/*
	Teamspeak 3 Winamp Now Playing Plugin
	Copyright (C) 2014 Screech (based on antihack3r's work)

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/
#pragma region Defines, Includes and Variables
#define PLUGIN_API_VERSION 22

#define DEBUG_USAGE_UPDATE
//#define ENABLE_QUEUED_SUB_CODE
#define _CRT_SECURE_NO_WARNINGS
#include "nowplaying_plugin.h"
#include "winamp.h"
#include "vlc.h"
#include "lightalloy.h"
#include "google.h"
#include "spotify.h"
#include "spider.h"
#include "wmp.h"
#include <iostream>
#include <vector>
#include <string>
using namespace std;



#define _strcpy(dest, destSize, src) strcpy_s(dest, destSize, src)
#define snprintf sprintf_s

CRITICAL_SECTION cs;


static struct TS3Functions ts3Functions;
int iRun = 0;
int iIsRunning = 0;
int iFoundNothingLastTime = 0;
time_t tMyLastUpdate;
HANDLE hThread;
static char* pluginID = NULL;
static struct TrackInfo lastInfo;
char chChannelMsg[300];
char chApplications[300];
char chBoundToUniqueID[300];
int iEnableApplications;
int iEnableAutoChannelMsg;
int iRefreshRate = 500;
int iScanWinamp = 1;
int iScanVlc = 1;
int iScanLightalloy = 1;
int iScanGoogle = 0;
int iScanSpotify = 1;
int iScanSpider = 1;
int iScanWmp = 1;
int iSlotForNextClientID = 0;
int iLonelyMute = 0;
anyID PlayersToRequestPlayingInfoFrom[3000];
vector<anyID> activePlayers;
vector<uint64> activeServerHandlers;
vector<string> activeSongs;
vector<time_t> activeUpdateTime;


#define PATH_BUFSIZE 512
#define COMMAND_BUFSIZE 1024
#define INFODATA_BUFSIZE 1024
#define SERVERINFO_BUFSIZE 256
#define CHANNELINFO_BUFSIZE 512
#define RETURNCODE_BUFSIZE 128
#define MAX_TTL_SINCE_LAST_UPDATE 1200

#define STRING_NOT_FOUND "%Not Found%"
#define INT_NOT_FOUND 999
#define CONFIG_FILE "now_playing_plugin.ini"
#define CHANNEL_DEFAULT "I'm listening to [b]{title}[/b]."
#define APPLICATION_DEFAULT "{title}"
#pragma endregion 

/* Vector for clientplugin command return codes. */
vector<string> sendPluginCommandReturnCodes;

/* Helper function to convert wchar_T to Utf-8 encoded strings on Windows */
static int wcharToUtf8(const wchar_t* str, char** result)
{
	int outlen = WideCharToMultiByte(CP_UTF8, 0, str, -1, 0, 0, 0, 0);
	*result = (char*)malloc(outlen);
	if (WideCharToMultiByte(CP_UTF8, 0, str, -1, *result, outlen, 0, 0) == 0)
	{
		*result = NULL;
		return -1;
	}
	return 0;
}

const char* ts3plugin_name()
{
	return "Now Playing";
}

const char* ts3plugin_version()
{
	return "0.15.4031";
}

int ts3plugin_apiVersion()
{
	return PLUGIN_API_VERSION;
}

const char* ts3plugin_author()
{
#ifdef _WIN32
	static char* result = NULL;
	if (!result)
	{
		const wchar_t* name = L"Mr. ىϲrе‍‍еϲh";
		if (wcharToUtf8(name, &result) == -1)
		{
			result = "Screech";
		}
	}
	return result;
#else
	return "Screech";
#endif 
}

const char* ts3plugin_description()
{
	return "Displays your current playing track.\n\nBased on antihack3r's Now_playing plugin\n\nSee what songs other users are playing and optionally broadcast the song your listening to.\n\nIf you want to broadcast your own songs, you need to edit the config file (see documention)\n//updated by Leibi from https://teamspeak-plugins.org";
}

void ts3plugin_setFunctionPointers(const struct TS3Functions funcs)
{
	ts3Functions = funcs;
}

int ts3plugin_init()
{

	InitializeCriticalSection(&cs);

	loadInitSettings();

	iRun = 1;
	hThread = CreateThread(NULL, 0, MainLoop, NULL, 0, NULL);
	if (hThread == 0)
	{
		printf("\tError could not create Thread %d\n", GetLastError());
		return 1;
	}

	return 0;  /* 0 = success, 1 = failure, -2 = failure but client will not show a "failed to load" warning */
	/* -2 is a very special case and should only be used if a plugin displays a dialog (e.g. overlay) asking the user to disable
	 * the plugin again, avoiding the show another dialog by the client telling the user the plugin failed to load.
	 * For normal case, if a plugin really failed to load because of an error, the correct return value is 1. */
}

void ts3plugin_shutdown()
{
	printf("NowPlaying: starting shutdown.\n");
	if (iRun && hThread != 0)
	{
		DWORD dWFSOreturn = WAIT_TIMEOUT;
		time_t loopStartTime, currentTime;
		iRun = 0;
		printf("\tEntering while loop....\n");
		time(&loopStartTime);
		time(&currentTime);
		while (iIsRunning && loopStartTime + 10 > currentTime)
		{
			printf("\tWaiting .05 second...\n");
			Sleep(50);
			time(&currentTime);
			continue;
		}
		printf("\tExited while loop.\n");
		//Sleep (3000);
		try
		{
			dWFSOreturn = WaitForSingleObject(hThread, iRefreshRate);
			switch (dWFSOreturn)
			{
			case WAIT_ABANDONED:
			{
				printf("\tWaitForSingleObject result: WAIT_ABANDONED.\n");
				//Sleep (3000);
				break;
			}
			case WAIT_OBJECT_0:
			{
				printf("\tWaitForSingleObject result: WAIT_OBJECT_0.\n");
				printf("\tHave single thread.\n");
				//Sleep (3000);
				CloseHandle(hThread);
				printf("\tThread Closed.\n");
				break;
			}
			case WAIT_TIMEOUT:
			{
				printf("\tWaitForSingleObject result: WAIT_TIMEOUT.\n");
				//Sleep (3000);
				break;
			}
			case WAIT_FAILED:
			{
				printf("\tWaitForSingleObject result: WAIT_FAILED.\n");
				//Sleep (3000);
				break;
			}
			}
		}
		catch (...)
		{
			printf("Something errored and now the catch is running.\n");
			Sleep(10000);
		}
		/* printf("\tHave single thread.\n");
		Sleep (3000);
		CloseHandle (hThread);
		printf("\tThread Closed.\n"); */
		printf("\tAbout to delete critical section.\n");
		//Sleep (3000);
		DeleteCriticalSection(&cs);
		printf("\tCritical Section deleted.\n");
	}

	if (pluginID)
	{
		printf("\tFreeing pluginID.\n");
		free(pluginID);
		pluginID = NULL;
		printf("\tPluginID freed.\n");
	}
	printf("NowPlaying: Shutdown.\n");
}

/****************************** Optional functions ********************************/\

/* Tell client if plugin offers a configuration window. If this function is not implemented, it's an assumed "does not offer" (PLUGIN_OFFERS_NO_CONFIGURE).
   While not used right now I plan want to expend to support config window*/
	int ts3plugin_offersConfigure()
{
	/*
	 * Return values:
	 * PLUGIN_OFFERS_NO_CONFIGURE         - Plugin does not implement ts3plugin_configure
	 * PLUGIN_OFFERS_CONFIGURE_NEW_THREAD - Plugin does implement ts3plugin_configure and requests to run this function in an own thread
	 * PLUGIN_OFFERS_CONFIGURE_QT_THREAD  - Plugin does implement ts3plugin_configure and requests to run this function in the Qt GUI thread
	 */
	return PLUGIN_OFFERS_NO_CONFIGURE;  /* In this case ts3plugin_configure does not need to be implemented */
}

/* Plugin might offer a configuration window. If ts3plugin_offersConfigure returns 0, this function does not need to be implemented. */
void ts3plugin_configure(void* handle, void* qParentWidget)
{
}

void ts3plugin_registerPluginID(const char* id)
{
	const size_t sz = strlen(id) + 1;

	printf("NowPlaying: registering pluginID\n");

	pluginID = (char*)malloc(sz * sizeof(char));
	_strcpy(pluginID, sz, id);
	printf("\tregisterPluginID = %s\n", pluginID);
}


const char* ts3plugin_commandKeyword()
{
	return "nowplaying";
}

int ts3plugin_processCommand(uint64 serverConnectionHandlerID, const char* command)
{
	char buf[COMMAND_BUFSIZE];
	char *s, *param1 = NULL, *param2 = NULL;
	int i = 0;
	enum { CMD_NONE = 0, CMD_FOO, CMD_SENDPLUGINCOMMAND, CMD_USAGE } cmd = CMD_NONE;
	char* context = NULL;


	printf("NowPlaying: process command: '%s'\n", command);
	if (strlen(command) + 1 > COMMAND_BUFSIZE)
	{ //buffer too small, return before client crashes
		printf("\tReceived command too large for buffer.\n");
		ts3Functions.printMessageToCurrentTab("NowPlaying: Received a command that was too large to process.");
		return 1;
	}
	_strcpy(buf, COMMAND_BUFSIZE, command);
	s = strtok_s(buf, " ", &context);
	while (s != NULL)
	{
		if (i == 0)
		{
			if (!strcmp(s, "foo"))
			{
				cmd = CMD_FOO;
			}
			else if (!strcmp(s, "test"))
			{
				cmd = CMD_SENDPLUGINCOMMAND;
			}
			else if (!strcmp(s, "usage"))
			{
				cmd = CMD_USAGE;
			}
		}
		else if (i == 1)
		{
			param1 = s;
		}
		else
		{
			param2 = s;
		}
		s = strtok_s(NULL, " ", &context);
		i++;
	}
	switch (cmd)
	{
	case CMD_NONE:
		return 1; /* Commnd not handled by plugin */
	case CMD_FOO:
	{
		anyID ownID;
		uint64 channelID;
		if (ts3Functions.getClientID(serverConnectionHandlerID, &ownID) == ERROR_ok
			&& ts3Functions.getChannelOfClient(serverConnectionHandlerID, ownID, &channelID) == ERROR_ok)
		{
			spamChannel(serverConnectionHandlerID, channelID);
		}
		break;
	}
	case CMD_SENDPLUGINCOMMAND:
	{
		if (param1)
		{
			/* Send plugin command to all clients in current channel. In this case targetIds is unused and can be NULL. */
			if (pluginID)
			{
				/* See ts3plugin_registerPluginID for how to obtain a pluginID */
				ts3Functions.sendPluginCommand(serverConnectionHandlerID, pluginID, param1, PluginCommandTarget_CURRENT_CHANNEL, NULL, NULL);
			}
			else
			{
				printf("\tFailed to send plugin command, was not registered.\n");
			}
		}
		else
		{
			ts3Functions.printMessageToCurrentTab("Missing command parameter.");
		}
		break;
	}
	case CMD_USAGE:
	{
		if (pluginID)
		{
			anyID myID;

			printf("\tUser passed usage command.\n");
			if (!pluginID)
			{
				printf("\tUnable to send Plugin command, plugin not registered.\n");
				break;
			}
			if (ts3Functions.getClientID(serverConnectionHandlerID, &myID) != ERROR_ok)
			{
				printf("\tFailed to get my ID.\n");
				break;
			}
			char command[COMMAND_BUFSIZE];
			snprintf(command, sizeof(command), "usageReport?%d", myID);
			ts3Functions.sendPluginCommand(serverConnectionHandlerID, pluginID, command, PluginCommandTarget_SERVER, NULL, NULL);
		}
	}
	}
	return 0; /* handled */
}

void ts3plugin_pluginEvent(unsigned short data, const char* message)
{
	char type, subtype;

	printf("PLUGIN: pluginEvent data = %u\n", data);
	if (message)
	{
		printf("Message: %s\n", message);
	}

	type = data >> 8;
	subtype = data & 0xFF;
	printf("Type = %d, subtype = %d\n", type, subtype);

	switch (type)
	{
	case PLUGIN_EVENT_TYPE_HOTKEY:
		switch (subtype)
		{
		case PLUGIN_EVENT_SUBTYPE_HOTKEY_TOGGLE_MICRO_ON:
			printf("Micro on\n");
			break;
		case PLUGIN_EVENT_SUBTYPE_HOTKEY_TOGGLE_MICRO_OFF:
			printf("Micro off\n");
			break;
		case PLUGIN_EVENT_SUBTYPE_HOTKEY_TOGGLE_SPEAKER_ON:
			printf("Speaker on\n");
			break;
		case PLUGIN_EVENT_SUBTYPE_HOTKEY_TOGGLE_SPEAKER_OFF:
			printf("Speaker off\n");
			break;
		case PLUGIN_EVENT_SUBTYPE_HOTKEY_TOGGLE_AWAY_ON:
			printf("Away on\n");
			break;
		case PLUGIN_EVENT_SUBTYPE_HOTKEY_TOGGLE_AWAY_OFF:
			printf("Away off\n");
			break;
		case PLUGIN_EVENT_SUBTYPE_HOTKEY_ACTIVATE_MICRO:
			printf("Activate micro\n");
			break;
		case PLUGIN_EVENT_SUBTYPE_HOTKEY_ACTIVATE_SPEAKER:
			printf("Activate speaker\n");
			break;
		case PLUGIN_EVENT_SUBTYPE_HOTKEY_ACTIVATE_AWAY:
			printf("Activate away\n");
			break;
		case PLUGIN_EVENT_SUBTYPE_HOTKEY_DEACTIVATE_MICRO:
			printf("Deactivate micro\n");
			break;
		case PLUGIN_EVENT_SUBTYPE_HOTKEY_DEACTIVATE_SPEAKER:
			printf("Deactivate speakers\n");
			break;
		case PLUGIN_EVENT_SUBTYPE_HOTKEY_DEACTIVATE_AWAY:
			printf("Deactivate away\n");
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

/*
 * Implement the following three functions when the plugin should display a line in the server/channel/client info.
 * If any of ts3plugin_infoTitle, ts3plugin_infoData or ts3plugin_freeMemory is missing, the info text will not be displayed.
 */

 /* Static title shown in the left column in the info frame */
const char* ts3plugin_infoTitle()
{
	return "Now Playing";
}

/*
 * Dynamic content shown in the right column in the info frame. Memory for the data string needs to be allocated in this
 * function. The client will call ts3plugin_freeMemory once done with the string to release the allocated memory again.
 * Check the parameter "type" if you want to implement this feature only for specific item types. Set the parameter
 * "data" to NULL to have the client ignore the info data.
 */
void ts3plugin_infoData(uint64 serverConnectionHandlerID, uint64 id, enum PluginItemType type, char** data)
{
	char chFinalString[2048];

	switch (type)
	{
	case PLUGIN_CLIENT:
	{
		static struct TrackInfo clientInfo;
		int clientDataRow;
		time_t currentTime;

		printf("NowPlaying.ts3plugin_infoData: Client selected.\n");
		if (GetRowForUser_Server(serverConnectionHandlerID, (anyID)id, &clientDataRow) == 1)
		{
			printf("\tFound a record for selected client.\n");
			time(&currentTime);
			if (currentTime - activeUpdateTime.at(clientDataRow) < MAX_TTL_SINCE_LAST_UPDATE)
			{
				char chSong[1024];

				printf("\tRecord for selected client has not yet expired.\n");
				snprintf(chSong, sizeof(chSong), "%s", activeSongs[clientDataRow].c_str());
				strcpy(clientInfo.chTitle, chSong);
				if (FormatTitle(chFinalString, sizeof(chFinalString), chApplications, clientInfo) != 1)
				{
					printf("NowPlaying.ts3plugin_infoData: Failed to format Title.\n");
					data = NULL;
					return;
				}
			}
			else
			{
				printf("\tData for selected client ID has expired, removing record.");
				activePlayers.erase(activePlayers.begin() + clientDataRow);
				activeServerHandlers.erase(activeServerHandlers.begin() + clientDataRow);
				activeSongs.erase(activeSongs.begin() + clientDataRow);
				activeUpdateTime.erase(activeUpdateTime.begin() + clientDataRow);
				data = NULL;
				return;
			}
		}
		else
		{
			printf("\tNo data found for selected user.\n");
			data = NULL;
			return;
		}
		break;
	}
	default:
		//printf("Unsupported item type: %d\n", type);
		data = NULL; /* Ignore */
		return;
	}
	if (strlen(chFinalString) + 1 > INFODATA_BUFSIZE)
	{ //buffer too small, return before client crashes
		printf("\tSong name too large for buffer.\n");
		snprintf(chFinalString, 50, "<Title info too long to display>");
	}
	*data = (char*)malloc(2048 * sizeof(char));  /* Must be allocated in the plugin! */
	snprintf(*data, INFODATA_BUFSIZE, "%s", chFinalString);
	/* testing without the freememory, for some reason it is crashing ts3
	printf("Now Playing: about to call freememory.\n");
	Sleep(3000);
	ts3Functions.freeMemory(chFinalString);
	*/
}

/* Required to release the memory for parameter "data" allocated in ts3plugin_infoData */
void ts3plugin_freeMemory(void* data)
{
	free(data);
}

int ts3plugin_requestAutoload()
{
	return 1;  /* 1 = request autoloaded, 0 = do not request autoload */
}

/* Helper function to create a menu item */
static struct PluginMenuItem* createMenuItem(enum PluginMenuType type, int id, const char* text, const char* icon)
{
	struct PluginMenuItem* menuItem = (struct PluginMenuItem*)malloc(sizeof(struct PluginMenuItem));
	menuItem->type = type;
	menuItem->id = id;
	_strcpy(menuItem->text, PLUGIN_MENU_BUFSZ, text);
	_strcpy(menuItem->icon, PLUGIN_MENU_BUFSZ, icon);
	return menuItem;
}

/* Some makros to make the code to create menu items a bit more readable */
#define BEGIN_CREATE_MENUS(x) const size_t sz = x + 1; size_t n = 0; *menuItems = (struct PluginMenuItem**)malloc(sizeof(struct PluginMenuItem*) * sz);
#define CREATE_MENU_ITEM(a, b, c, d) (*menuItems)[n++] = createMenuItem(a, b, c, d);
#define END_CREATE_MENUS (*menuItems)[n++] = NULL; assert(n == sz);

/*
 * Menu IDs for this plugin. Pass these IDs when creating a menuitem to the TS3 client. When the menu item is triggered,
 * ts3plugin_onMenuItemEvent will be called passing the menu ID of the triggered menu item.
 * These IDs are freely choosable by the plugin author. It's not really needed to use an enum, it just looks prettier.
 */
enum
{
	MENU_NP_CLIENT_PM = 1,
	MENU_NP_GLOBAL_CHANNEL_SPAM,
	MENU_NP_GLOBAL_PLAYER_STATUS,
	MENU_NP_GLOBAL_SPACER,
	MENU_NP_GLOBAL_TOGGLE_WINAMP,
	MENU_NP_GLOBAL_TOGGLE_VLC,
	MENU_NP_GLOBAL_TOGGLE_LIGHTALLOY,
	MENU_NP_GLOBAL_TOGGLE_GOOGLE,
	MENU_NP_GLOBAL_TOGGLE_SPOTIFY,
	MENU_NP_GLOBAL_TOGGLE_SPIDER,
	MENU_NP_GLOBAL_TOGGLE_WMP
};

/*
 * Initialize plugin menus.
 * This function is called after ts3plugin_init and ts3plugin_registerPluginID. A pluginID is required for plugin menus to work.
 * Both ts3plugin_registerPluginID and ts3plugin_freeMemory must be implemented to use menus.
 * If plugin menus are not used by a plugin, do not implement this function or return NULL.
 */
void ts3plugin_initMenus(struct PluginMenuItem*** menuItems, char** menuIcon)
{
	/*
	 * Create the menus
	 * There are three types of menu items:
	 * - PLUGIN_MENU_TYPE_CLIENT:  Client context menu
	 * - PLUGIN_MENU_TYPE_CHANNEL: Channel context menu
	 * - PLUGIN_MENU_TYPE_GLOBAL:  "Plugins" menu in menu bar of main window
	 *
	 * Menu IDs are used to identify the menu item when ts3plugin_onMenuItemEvent is called
	 *
	 * The menu text is required, max length is 128 characters
	 *
	 * The icon is optional, max length is 128 characters. When not using icons, just pass an empty string.
	 * Icons are loaded from a subdirectory in the TeamSpeak client plugins folder. The subdirectory must be named like the
	 * plugin filename, without dll/so/dylib suffix
	 * e.g. for "test_plugin.dll", icon "1.png" is loaded from <TeamSpeak 3 Client install dir>\plugins\test_plugin\1.png
	 */

	BEGIN_CREATE_MENUS(11);  /* IMPORTANT: Number of menu items must be correct! */
	CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_CLIENT, MENU_NP_CLIENT_PM, "PM Song Info to Client", "");
	CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_GLOBAL, MENU_NP_GLOBAL_CHANNEL_SPAM, "Send Song Info to Current Channel", "");
	CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_GLOBAL, MENU_NP_GLOBAL_PLAYER_STATUS, "Display Current Player Scanning Status", "");
	CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_GLOBAL, MENU_NP_GLOBAL_SPACER, "---------------------------------", "");
	CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_GLOBAL, MENU_NP_GLOBAL_TOGGLE_WINAMP, "Toggle Winamp Support", "");
	CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_GLOBAL, MENU_NP_GLOBAL_TOGGLE_VLC, "Toggle VLC Support", "");
	CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_GLOBAL, MENU_NP_GLOBAL_TOGGLE_LIGHTALLOY, "Toggle Light Alloy Support", "");
	CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_GLOBAL, MENU_NP_GLOBAL_TOGGLE_GOOGLE, "Toggle Google Play Support", "");
	CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_GLOBAL, MENU_NP_GLOBAL_TOGGLE_SPOTIFY, "Toggle Spotify Support", "");
	CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_GLOBAL, MENU_NP_GLOBAL_TOGGLE_SPIDER, "Toggle Spider Support", "");
	CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_GLOBAL, MENU_NP_GLOBAL_TOGGLE_WMP, "Toggle WMP Support", "");
	END_CREATE_MENUS;  /* Includes an assert checking if the number of menu items matched */
	ts3Functions.setPluginMenuEnabled(pluginID, MENU_NP_GLOBAL_SPACER, 0);

	/*
	 * Specify an optional icon for the plugin. This icon is used for the plugins submenu within context and main menus
	 * If unused, set menuIcon to NULL
	 */
	*menuIcon = (char*)malloc(PLUGIN_MENU_BUFSZ * sizeof(char));
	_strcpy(*menuIcon, PLUGIN_MENU_BUFSZ, "np.png");

	/*
	 * Menus can be enabled or disabled with: ts3Functions.setPluginMenuEnabled(pluginID, menuID, 0|1);
	 * Test it with plugin command: /test enablemenu <menuID> <0|1>
	 * Menus are enabled by default. Please note that shown menus will not automatically enable or disable when calling this function to
	 * ensure Qt menus are not modified by any thread other the UI thread. The enabled or disable state will change the next time a
	 * menu is displayed.
	 */
	 /* For example, this would disable MENU_ID_GLOBAL_2: */
	 /* ts3Functions.setPluginMenuEnabled(pluginID, MENU_ID_GLOBAL_2, 0); */

	 /* All memory allocated in this function will be automatically released by the TeamSpeak client later by calling ts3plugin_freeMemory */
}



void ts3plugin_onConnectStatusChangeEvent(uint64 serverConnectionHandlerID, int newStatus, unsigned int errorNumber)
{
	switch (newStatus)
	{
	case STATUS_DISCONNECTED:
	{
		int removedRecords = 0;

		printf("NowPlaying: Cleaning data for stopped server.\n");
		removeAllServerHandlerRecords(serverConnectionHandlerID, &removedRecords);
		if (removedRecords) printf("\tRemoved %d record(s).\n", removedRecords);
		break;
	}
	case STATUS_CONNECTION_ESTABLISHED:
		printf("NowPlaying: Server (%d) connection established.\n", serverConnectionHandlerID);
		if (!iFoundNothingLastTime)
		{
			anyID myID;
			char chFinalString[2048];
			uint64 myChannel;

			if (!pluginID)
			{
				printf("\tUnable to broadcast song info, plugin not registered.\n");
				break;
			}
			if (ts3Functions.getClientID(serverConnectionHandlerID, &myID) != ERROR_ok)
			{
				printf("\tFailed to get my ID.\n");
				break;
			}
			if (FormatTitle(chFinalString, sizeof(chFinalString), chApplications, lastInfo) != 1)
			{
				printf("\tFailed to format Title.\n");
				break;
			}
			{
				char command[COMMAND_BUFSIZE + sizeof(chFinalString)];

				snprintf(command, sizeof(command), "playing╜%d╜%s", myID, chFinalString);
				ts3Functions.sendPluginCommand(serverConnectionHandlerID, pluginID, command, PluginCommandTarget_CURRENT_CHANNEL_SUBSCRIBED_CLIENTS, NULL, NULL);
				if (ts3Functions.getChannelOfClient(serverConnectionHandlerID, myID, &myChannel) == ERROR_ok)
				{
					processChannelSubOrMeMoved(serverConnectionHandlerID, myChannel);
				}
			}


		}
	}
}
int ts3plugin_onServerErrorEvent(uint64 serverConnectionHandlerID, const char* errorMessage, unsigned int error, const char* returnCode, const char* extraMessage)
{
	printf("PLUGIN: onServerErrorEvent %llu %s %d %s\n", (long long unsigned int)serverConnectionHandlerID, errorMessage, error, (returnCode ? returnCode : ""));
	if (returnCode)
	{
		// Need to see if it is a plugin command error
		unsigned int checkRec = 0;

		if (sendPluginCommandReturnCodes.size() > 0)
		{
			while (checkRec < sendPluginCommandReturnCodes.size())
			{
				//int currentRecReturnCode = sendPluginCommandReturnCodes.at(checkRec);
				if (sendPluginCommandReturnCodes.at(checkRec) == returnCode)
				{
					sendPluginCommandReturnCodes.erase(sendPluginCommandReturnCodes.begin() + checkRec);
					return 1;
				}
				else
				{
					checkRec++;
					continue;
				}
			}
		}

	}
	return 0;
}
void ts3plugin_onServerStopEvent(uint64 serverConnectionHandlerID, const char* shutdownMessage)
{ // remove any song data connected to this
	int removedRecords = 0;

	printf("NowPlaying: Cleaning data for stopped server.\n");
	removeAllServerHandlerRecords(serverConnectionHandlerID, &removedRecords);
	if (removedRecords) printf("\tRemoved %d record(s).\n", removedRecords);
}
void ts3plugin_onUpdateClientEvent(uint64 serverConnectionHandlerID, anyID clientID, anyID invokerID, const char* invokerName, const char* invokerUniqueIdentifier)
{
	CheckIfAlone(serverConnectionHandlerID);
}
void ts3plugin_onClientMoveEvent(uint64 serverConnectionHandlerID, anyID clientID, uint64 oldChannelID, uint64 newChannelID, int visibility, const char* moveMessage)
{
	CheckIfAlone(serverConnectionHandlerID);
	processClientMoveKickVisCheck(serverConnectionHandlerID, clientID, visibility);
}
void ts3plugin_onClientMoveMovedEvent(uint64 serverConnectionHandlerID, anyID clientID, uint64 oldChannelID, uint64 newChannelID, int visibility, anyID moverID, const char* moverName, const char* moverUniqueIdentifier, const char* moveMessage)
{
	CheckIfAlone(serverConnectionHandlerID);
	processClientMoveKickVisCheck(serverConnectionHandlerID, clientID, visibility);
}
void ts3plugin_onClientKickFromChannelEvent(uint64 serverConnectionHandlerID, anyID clientID, uint64 oldChannelID, uint64 newChannelID, int visibility, anyID kickerID, const char* kickerName, const char* kickerUniqueIdentifier, const char* kickMessage)
{
	CheckIfAlone(serverConnectionHandlerID);
	processClientMoveKickVisCheck(serverConnectionHandlerID, clientID, visibility);
}
void ts3plugin_onClientKickFromServerEvent(uint64 serverConnectionHandlerID, anyID clientID, uint64 oldChannelID, uint64 newChannelID, int visibility, anyID kickerID, const char* kickerName, const char* kickerUniqueIdentifier, const char* kickMessage)
{
	CheckIfAlone(serverConnectionHandlerID);
	removeRecord(serverConnectionHandlerID, clientID);
}
void ts3plugin_onChannelSubscribeEvent(uint64 serverConnectionHandlerID, uint64 channelID)
{
	processChannelSubOrMeMoved(serverConnectionHandlerID, channelID);
}
void ts3plugin_onPluginCommandEvent(uint64 serverConnectionHandlerID, const char* pluginName, const char* pluginCommand)
{
	char buf[COMMAND_BUFSIZE];
	char *s, *param1 = NULL, *param2 = NULL, *param3 = NULL;
	int i = 0;
	const char* cDelimiter = "╜";
	size_t pluginCommandSize = COMMAND_BUFSIZE;
	enum { CMD_NONE = 0, CMD_SONGPLAYING, CMD_REQUESTSONG, CMD_USAGE_REQUEST, CMD_USAGE_REPLY, CMD_USAGE_REPLY2, CMD_NOTPLAYING } cmd = CMD_NONE;
	char* context = NULL;

	//printf("Now Playing: onPluginCommandEvent\n\tserverConnectionHandlerID %d\n\tpluginName %s\n\tpluginCommand %s\n", serverConnectionHandlerID, pluginName, pluginCommand);
	pluginCommandSize = strlen(pluginCommand);
	if (pluginCommandSize + 1 > COMMAND_BUFSIZE)
	{ //buffer too small, return before client crashes
//printf("\tReceived command too large for buffer.\n");
		ts3Functions.printMessageToCurrentTab("NowPlaying: Received a plugin command that was too large to process. (normally caused when someone plays a file with a REALLY REALLY long title)");
		return;
	}
	_strcpy(buf, COMMAND_BUFSIZE, pluginCommand);
	printf("NowPlaying: Received Command: %s\n", pluginCommand);

	//add code here to test if "╜" is in the command, if not set cDelimiter = "?" to support Plugin Command Event from older version of plugin or backward compatible commands from the new plugin
	if (string(buf).find("╜") == std::string::npos) cDelimiter = "?";

	s = strtok_s(buf, cDelimiter, &context);

	while (s != NULL)
	{
		printf("\tCurrent Command Section: %s\n", s);
		if (i == 0)
		{
			if (!strcmp(s, "playing"))
			{
				cmd = CMD_SONGPLAYING;
			}
			else if (!strcmp(s, "requestSong"))
			{
				cmd = CMD_REQUESTSONG;
			}
			else if (!strcmp(s, "usageReport"))
			{
				cmd = CMD_USAGE_REQUEST;
			}
			else if (!strcmp(s, "usageReply"))
			{
				cmd = CMD_USAGE_REPLY;
			}
			else if (!strcmp(s, "usageReply2"))
			{ //updated to support receiving UID instead of clid
				cmd = CMD_USAGE_REPLY2;
			}
			else if (!strcmp(s, "notPlaying"))
			{
				cmd = CMD_NOTPLAYING;
			}
			else
			{
				printf("NowPlaying: Unknown command passed: %s\n", s);
			}
		}
		else if (i == 1)
		{
			param1 = s;
		}
		else if (i == 2)
		{
			param2 = s;
		}
		else
		{
			if (param3 != NULL)
			{
				char temp[COMMAND_BUFSIZE];
				snprintf(temp, COMMAND_BUFSIZE, "%s%s%s", param3, cDelimiter, s);
				param3 = temp;
			}
			else
			{
				param3 = s;
			}
			//printf("\tparam3 = %s\n", param3);
		}
		s = strtok_s(NULL, cDelimiter, &context);
		i++;
	}
	switch (cmd)
	{
	case CMD_NONE:
	{
		printf("Extracted info:\n\tCommand: Unknown\n\tparam1: %s\n\tparam2: %s\n\tparam3: %s\n\n", param1, param2, param3);
		break;
	}
	case CMD_SONGPLAYING:
		if (param2)
		{
			time_t currentTime;
			struct tm * timeinfo;
			anyID playerID;
			int existingRec = -1;
			string playingSong;


			playerID = (int)atoi(param1);
			time(&currentTime);
			timeinfo = localtime(&currentTime);
			//snprintf(playingSong, sizeof(playingSong), "%s", param2);
			//strcpy(playingSong, param2);
			playingSong = string(param2);
			printf("NowPlaying: Received song playing info.\n");
			printf("\tExtracted info:\n\tCommand: playing\n\tPlayer: %s\n\tSong: %s\n", param1, param2);
			//printf("\tCurrent time: %s\n\n", asctime(timeinfo));
			EnterCriticalSection(&cs);
			if (GetRowForUser_Server(serverConnectionHandlerID, playerID, &existingRec) == 1)
			{
				activeSongs[existingRec] = playingSong;
				activeUpdateTime[existingRec] = currentTime;
				printf("NowPlaying: updated existing record in activeSongs[%d] to %s\n", existingRec, activeSongs[existingRec].c_str());
			}
			else
			{
				activePlayers.push_back(playerID);
				activeServerHandlers.push_back(serverConnectionHandlerID);
				activeSongs.push_back(playingSong);
				activeUpdateTime.push_back(currentTime);
				printf("NowPlaying: New record added to activeSongs: %s\n", activeSongs.back().c_str());
			}
			ts3Functions.requestClientVariables(serverConnectionHandlerID, playerID, NULL);
			LeaveCriticalSection(&cs);
		}
		break;
	case CMD_REQUESTSONG:
		if (param1)
		{
			anyID myID, toIDs[2];
			char command[COMMAND_BUFSIZE];
			char returnCode[RETURNCODE_BUFSIZE];

			printf("NowPlaying: Received playing song request.\n\tRequester: %s\n", param1);
			toIDs[0] = (int)atoi(param1);
			toIDs[1] = 0;
			if (ts3Functions.getClientID(serverConnectionHandlerID, &myID) != ERROR_ok)
			{
				printf("\tFailed to get my ID.\n");
				return;
			}
			if (!iFoundNothingLastTime)
			{
				char chFinalString[2048];

				if (FormatTitle(chFinalString, sizeof(chFinalString), chApplications, lastInfo) != 1)
				{
					printf("\tFailed to format Title.\n");
					return;
				}
				snprintf(command, sizeof(command), "playing╜%d╜%s", myID, chFinalString);
			}
			else
			{
				printf("SENDING NOT PLAYING\n");
				snprintf(command, sizeof(command), "notPlaying?%d", myID);
			}
			ts3Functions.createReturnCode(pluginID, returnCode, RETURNCODE_BUFSIZE);
			sendPluginCommandReturnCodes.push_back(returnCode);
			ts3Functions.sendPluginCommand(serverConnectionHandlerID, pluginID, command, PluginCommandTarget_CLIENT, toIDs, returnCode);
			printf("\tSent song request reply sent to client (%d).\n", toIDs[0]);
		}
		break;
	case CMD_USAGE_REQUEST:
		if (param1)
		{
			anyID myID;
			anyID toID;

			toID = (int)atoi(param1);

			printf("NowPlaying: Received usage info request.\n\tRequester: %s\n\n", param1);
			if (ts3Functions.getClientID(serverConnectionHandlerID, &myID) == ERROR_ok)
			{
				anyID toIDs[2];
				const char* pluginVersion;
				char command[COMMAND_BUFSIZE];
				char returnCode[RETURNCODE_BUFSIZE];
				char *myUID;

				//toID = (anyID)param1;
				toIDs[0] = toID;
				toIDs[1] = 0;
				pluginVersion = ts3plugin_version();
#ifdef DEBUG_USAGE_UPDATE
				ts3Functions.getClientSelfVariableAsString(serverConnectionHandlerID, CLIENT_UNIQUE_IDENTIFIER, &myUID);
				snprintf(command, sizeof(command), "usageReply2?%d?%s?%s", myID, myUID, pluginVersion);

#else
				snprintf(command, sizeof(command), "usageReply?%d?%s", myID, pluginVersion);
#endif 
				ts3Functions.createReturnCode(pluginID, returnCode, RETURNCODE_BUFSIZE);
				sendPluginCommandReturnCodes.push_back(returnCode);
				ts3Functions.sendPluginCommand(serverConnectionHandlerID, pluginID, command, PluginCommandTarget_CLIENT, toIDs, returnCode);
				printf("NowPlaying: Sent usage reply.\n");
			}
		}
		break;
	case CMD_USAGE_REPLY:
		if (param2)
		{
			char* nickName;
			anyID fromID;
			char message[150];
			int errCodeRet = ERROR_ok;

			printf("Nowplaying: Received usage reply.\n\tFrom: %s\n\tVersion: %s\n\n", param1, param2);
			fromID = (anyID)atoi(param1);
			errCodeRet = ts3Functions.getClientVariableAsString(serverConnectionHandlerID, fromID, CLIENT_NICKNAME, &nickName);
			if (errCodeRet == ERROR_ok)
			{
				snprintf(message, sizeof(message), "Version %s is being run by user:	[color=green]%s[/color]", param2, nickName);
				ts3Functions.printMessageToCurrentTab(message);
				ts3Functions.freeMemory(nickName);
			}
			else
			{ // User is not in a subbed channel
				snprintf(message, sizeof(message), "Version %s is being run by client ID:	[color=red]%d[/color]", param2, fromID);
				ts3Functions.printMessageToCurrentTab(message);
				/*
				char* errMess;
				if (ts3Functions.getErrorMessage(errCodeRet, &errMess) != ERROR_ok) {
					printf("NowPlaying: Failed to get nickname, error: %d\n", errCodeRet);
				} else {
					printf("NowPlaying: Failed to get nickname, error: %d (%s)\n", errCodeRet, errMess);
					ts3Functions.freeMemory(errMess);
				}
				*/
			}
		}
		break;
	case CMD_USAGE_REPLY2:
		if (param3)
		{
			anyID fromID;
			char* nickName;
			//char clientUID[50];
			char message[250];
			int errCodeRet = ERROR_ok;

			fromID = (anyID)atoi(param1);
			printf("NowPlaying: Received updated usage reply.\n\tFrom : %s\n\tVersion: %s\n\n", param1, param2);
			errCodeRet = ts3Functions.getClientVariableAsString(serverConnectionHandlerID, fromID, CLIENT_NICKNAME, &nickName);
			if (errCodeRet == ERROR_ok)
			{
				snprintf(message, sizeof(message), "Version %s is being run by user:	[url=client://%d/%s~%s]%s[/url]", param3, param1, param2, nickName, nickName);
				ts3Functions.printMessageToCurrentTab(message);
				ts3Functions.freeMemory(nickName);
			}
			else
			{ // User is not in a subbed channel
				snprintf(message, sizeof(message), "Version %s is being run by client ID:	[url=client://%d/%s~Unknown][color=red]%d[/color][/url]", param3, param1, param2, fromID);
				ts3Functions.printMessageToCurrentTab(message);
				/*
				char* errMess;
				if (ts3Functions.getErrorMessage(errCodeRet, &errMess) != ERROR_ok) {
					printf("NowPlaying: Failed to get nickname, error: %d\n", errCodeRet);
				} else {
					printf("NowPlaying: Failed to get nickname, error: %d (%s)\n", errCodeRet, errMess);
					ts3Functions.freeMemory(errMess);
				}
				*/
			}
		}
		break;
	case CMD_NOTPLAYING:
		if (param1)
		{
			anyID fromID;
			int existingRec = -1;

			printf("NowPlaying: User (%s) stopped playing.\n", param1);
			fromID = (anyID)atoi(param1);
			EnterCriticalSection(&cs);
			if (removeRecord(serverConnectionHandlerID, fromID) == 1) printf("\tRemoved record for this user.\n\n");
			ts3Functions.requestClientVariables(serverConnectionHandlerID, fromID, NULL);
			LeaveCriticalSection(&cs);
		}
	}
}

void ts3plugin_onMenuItemEvent(uint64 serverConnectionHandlerID, enum PluginMenuType type, int menuItemID, uint64 selectedItemID)
{
	printf("PLUGIN: onMenuItemEvent: serverConnectionHandlerID=%llu, type=%d, menuItemID=%d, selectedItemID=%llu\n", (long long unsigned int)serverConnectionHandlerID, type, menuItemID, (long long unsigned int)selectedItemID);
	switch (type)
	{
	case PLUGIN_MENU_TYPE_GLOBAL:
		// Global menu item was triggered. selectedItemID is unused and set to zero. 
		switch (menuItemID)
		{
		case MENU_NP_GLOBAL_CHANNEL_SPAM:
			spamChannel(serverConnectionHandlerID, selectedItemID);
			break;
		case MENU_NP_GLOBAL_PLAYER_STATUS:
			displayCurrentPlayerStatus();
			break;
		case MENU_NP_GLOBAL_TOGGLE_WINAMP:
			togglePlayer(PLAYER_APP_WINAMP);
			break;
		case MENU_NP_GLOBAL_TOGGLE_VLC:
			togglePlayer(PLAYER_APP_VLC);
			break;
		case MENU_NP_GLOBAL_TOGGLE_LIGHTALLOY:
			togglePlayer(PLAYER_APP_LIGHTALLOY);
			break;
		case MENU_NP_GLOBAL_TOGGLE_GOOGLE:
			togglePlayer(PLAYER_APP_GOOGLE);
			break;
		case MENU_NP_GLOBAL_TOGGLE_SPOTIFY:
			togglePlayer(PLAYER_APP_SPOTIFY);
			break;
		case MENU_NP_GLOBAL_TOGGLE_SPIDER:
			togglePlayer(PLAYER_APP_SPIDER);
			break;
		case MENU_NP_GLOBAL_TOGGLE_WMP:
			togglePlayer(PLAYER_APP_WMP);
			break;
		default:
			break;
		}
		break;
		/*
		case PLUGIN_MENU_TYPE_CHANNEL:
			// Channel contextmenu item was triggered. selectedItemID is the channelID of the selected channel
			switch(menuItemID) {
				case MENU_ID_CHANNEL_1:
					// Menu channel 1 was triggered
					break;
				case MENU_ID_CHANNEL_2:
					// Menu channel 2 was triggered
					break;
				case MENU_ID_CHANNEL_3:
					// Menu channel 3 was triggered
					break;
				default:
					break;
			}
			break;
			*/
	case PLUGIN_MENU_TYPE_CLIENT:
		/* Client contextmenu item was triggered. selectedItemID is the clientID of the selected client */
		switch (menuItemID)
		{
		case MENU_NP_CLIENT_PM:
			spamTarget(serverConnectionHandlerID, selectedItemID, 1);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

int loadInitSettings()
{
	char chFilePath[500];
	char chConfigPath[450];
	int updatedFile;

	time(&tMyLastUpdate);


	ts3Functions.getConfigPath(chConfigPath, sizeof(chConfigPath));
	printf("Now Playing: Loading/Initilizing Settings...\n\tchConfigPath = %s.\n", chConfigPath);
	snprintf(chFilePath, 500, "%s%s", chConfigPath, CONFIG_FILE);
	printf("\tchFilePath = %s.\n", chFilePath);

	updatedFile = 0;

	GetPrivateProfileString("config", "channel_message", STRING_NOT_FOUND,
		chChannelMsg, 300, chFilePath);
	if (strcmp(chChannelMsg, STRING_NOT_FOUND) == 0)
	{
		strcpy(chChannelMsg, CHANNEL_DEFAULT);
		WritePrivateProfileString("config", "channel_message", CHANNEL_DEFAULT, chFilePath);
		updatedFile = 1;
	}

	GetPrivateProfileString("config", "applications", STRING_NOT_FOUND,
		chApplications, 300, chFilePath);
	if (strcmp(chApplications, STRING_NOT_FOUND) == 0)
	{
		strcpy(chApplications, APPLICATION_DEFAULT);
		WritePrivateProfileString("config", "applications", APPLICATION_DEFAULT, chFilePath);
		updatedFile = 1;
	}

	GetPrivateProfileString("config", "bound_to_unique_id", STRING_NOT_FOUND,
		chBoundToUniqueID, 300, chFilePath);
	if (strcmp(chBoundToUniqueID, STRING_NOT_FOUND) == 0)
	{
		strcpy(chBoundToUniqueID, "");
		WritePrivateProfileString("config", "bound_to_unique_id", "", chFilePath);
		updatedFile = 1;
	}

	printf("\tchApplications = %s\n", chApplications);
	printf("\tchBoundToUniqueID = %s\n", chBoundToUniqueID);

	iEnableApplications = GetPrivateProfileInt("config", "enable_applications", INT_NOT_FOUND, chFilePath);
	if (iEnableApplications == INT_NOT_FOUND)
	{
		iEnableApplications = 1;
		WritePrivateProfileString("config", "enable_applications", "1", chFilePath);
		updatedFile = 1;
	}
	else if (iEnableApplications >= 1)
	{
		iEnableApplications = 1;
	}
	else
	{
		iEnableApplications = 0;
	}

	iEnableAutoChannelMsg = GetPrivateProfileInt("config", "auto_channel_message", INT_NOT_FOUND, chFilePath);
	if (iEnableAutoChannelMsg == INT_NOT_FOUND)
	{
		iEnableAutoChannelMsg = 0;
		WritePrivateProfileString("config", "auto_channel_message", "0", chFilePath);
		updatedFile = 1;
	}
	else if (iEnableAutoChannelMsg >= 1)
	{
		iEnableAutoChannelMsg = 1;
	}
	else
	{
		iEnableAutoChannelMsg = 0;
	}

	// Start new code for new player scanning options
	iScanWinamp = GetPrivateProfileInt("config", "scan_winamp", INT_NOT_FOUND, chFilePath);
	if (iScanWinamp == INT_NOT_FOUND)
	{
		iScanWinamp = 1;
		WritePrivateProfileString("config", "scan_winamp", "1", chFilePath);
		updatedFile = 1;
	}
	iScanVlc = GetPrivateProfileInt("config", "scan_vlc", INT_NOT_FOUND, chFilePath);
	if (iScanVlc == INT_NOT_FOUND)
	{
		iScanVlc = 1;
		WritePrivateProfileString("config", "scan_vlc", "1", chFilePath);
		updatedFile = 1;
	}
	iScanLightalloy = GetPrivateProfileInt("config", "scan_lightalloy", INT_NOT_FOUND, chFilePath);
	if (iScanLightalloy == INT_NOT_FOUND)
	{
		iScanLightalloy = 1;
		WritePrivateProfileString("config", "scan_lightalloy", "1", chFilePath);
		updatedFile = 1;
	}
	iScanGoogle = GetPrivateProfileInt("config", "scan_google", INT_NOT_FOUND, chFilePath);
	if (iScanGoogle == INT_NOT_FOUND)
	{
		iScanGoogle = 0;
		WritePrivateProfileString("config", "scan_google", "0", chFilePath);
		updatedFile = 1;
	}
	iScanSpotify = GetPrivateProfileInt("config", "scan_spotify", INT_NOT_FOUND, chFilePath);
	if (iScanSpotify == INT_NOT_FOUND)
	{
		iScanSpotify = 1;
		WritePrivateProfileString("config", "scan_spotify", "1", chFilePath);
		updatedFile = 1;
	}
	iScanSpider = GetPrivateProfileInt("config", "scan_spider", INT_NOT_FOUND, chFilePath);
	if (iScanSpider == INT_NOT_FOUND)
	{
		iScanSpider = 1;
		WritePrivateProfileString("config", "scan_spider", "1", chFilePath);
		updatedFile = 1;
	}
	iScanWmp = GetPrivateProfileInt("config", "scan_wmp", INT_NOT_FOUND, chFilePath);
	if (iScanWmp == INT_NOT_FOUND)
	{
		iScanWmp = 1;
		WritePrivateProfileString("config", "scan_wmp", "1", chFilePath);
		updatedFile = 1;
	}
	iLonelyMute = GetPrivateProfileInt("config", "mute_when_alone", INT_NOT_FOUND, chFilePath);
	if (iLonelyMute == INT_NOT_FOUND)
	{
		iLonelyMute = 1;
		WritePrivateProfileString("config", "mute_when_alone", "1", chFilePath);
		updatedFile = 1;
	}

	iRefreshRate = (GetPrivateProfileInt("config", "refresh_rate_milliseconds", 0, chFilePath));
	if (iRefreshRate < 500)
	{
		if (iRefreshRate == 0)
		{
			// setting is missing, must be an existing ini file, set to default
			printf("\trefresh_rate_milliseconds missing from config, adding with default value.\n");
			iRefreshRate = 1000;
			WritePrivateProfileString("config", "refresh_rate_milliseconds", "1000", chFilePath);
			updatedFile = 1;
		}
		else
		{
			printf("\trefresh_rate_milliseconds too low, setting to 500.\n");
			iRefreshRate = 500;
			WritePrivateProfileString("config", "refresh_rate_milliseconds", "500", chFilePath);
			updatedFile = 1;
		}
	}
	else if (iRefreshRate > 2000)
	{
		// limiting the mas refresh to 2 seconds
		printf("\trefresh_rate_milliseconds too high, setting to 2000.\n");
		iRefreshRate = 2000;
		WritePrivateProfileString("config", "refresh_rate_milliseconds", "2000", chFilePath);
		updatedFile = 1;
	}
	if (updatedFile = 1)
	{
		updatedFile = WritePrivateProfileString(0, 0, 0, chFilePath);
	}
	else
	{
		updatedFile = 1;
	}

	return updatedFile;
}
DWORD WINAPI MainLoop(LPVOID lpParam)
{
	uint64 *result;
	uint64 resultChannelID;
	struct TrackInfo info;
	int i = 0;

	//char chFinalString[2048];
	char chMessage[2048];
	int iFound = 0;
	int iSize = 0;

	char *chUniqueID;

	memset(&info, 0, sizeof(struct TrackInfo));
	memset(&lastInfo, 0, sizeof(struct TrackInfo));
	iIsRunning = 1;
	while (iRun)
	{
		EnterCriticalSection(&cs);

		memset(&info, 0, sizeof(struct TrackInfo));
		iFound = 0;

		if (iScanSpotify && spotify(&info, ts3Functions))
		{
			iFound = 1;
		}
		else if (iScanSpider && spider(&info, ts3Functions))
		{
			iFound = 1;
		}
		else if (iScanWinamp && winamp(&info, ts3Functions))
		{
			iFound = 1;
		}
		else if (iScanVlc && vlc(&info, ts3Functions))
		{
			iFound = 1;
		}
		else if (iScanLightalloy && lightalloy(&info, ts3Functions))
		{
			iFound = 1;
		}
		else if (iScanGoogle && google(&info, ts3Functions))
		{
			iFound = 1;
		}
		else if (iScanWmp && wmp(&info, ts3Functions))
		{
			iFound = 1;
		}

		if (iFound)
		{
			time_t currentTime;

			time(&currentTime);
			if ((memcmp(&info, &lastInfo, sizeof(struct TrackInfo)) != 0)
				| (currentTime - tMyLastUpdate > MAX_TTL_SINCE_LAST_UPDATE)
				| (currentTime < tMyLastUpdate))
			{
				anyID myID;
				char chFinalString[2048];
				int iSendSongInfo = 0;

				iSendSongInfo = iEnableApplications;
				printf("iEnableApplications = %d\niSendSongInfo = %d\n", iEnableApplications, iSendSongInfo);
				iFoundNothingLastTime = 0;

				memcpy(&lastInfo, &info, sizeof(struct TrackInfo));
				time(&tMyLastUpdate);
				printf("Now Playing: [%s]\n", info.chTitle);
				if (FormatTitle(chFinalString, sizeof(chFinalString), chApplications, lastInfo) != 1)
				{
					printf("NowPlaying.mainloop: Failed to format Title.\n");
					iSendSongInfo = 0;
				}
				i = 0;
				if (ts3Functions.getServerConnectionHandlerList(&result) == ERROR_ok)
				{
					while (result[i])
					{
						if (ts3Functions.getClientID(result[i], &myID) != ERROR_ok)
						{
							i++;
							continue;
						}


						if (iSendSongInfo == 1)
						{
							char command[COMMAND_BUFSIZE + sizeof(chFinalString)];

							snprintf(command, sizeof(command), "playing╜%d╜%s", myID, chFinalString);
							ts3Functions.sendPluginCommand(result[i], pluginID, command, PluginCommandTarget_CURRENT_CHANNEL_SUBSCRIBED_CLIENTS, NULL, NULL);
							printf("NowPlaying.mainloop: plugin command sent\n");
						}

						if (strlen(chBoundToUniqueID) == 28)
						{
							printf("NowPlaying.mainloop: chBoundToUniqueID has length of 28 chars\n");
							ts3Functions.getClientSelfVariableAsString(result[i], CLIENT_UNIQUE_IDENTIFIER, &chUniqueID);
							if (strcmp(chUniqueID, chBoundToUniqueID) != 0)
							{ /* Unique ID does not match */
								printf("NowPlaying.mainloop: unique ID does not match\n");
								ts3Functions.freeMemory(chUniqueID);
								i++;
								continue;
							}
							ts3Functions.freeMemory(chUniqueID);
						}

						printf("NowPlaying.mainloop: unique ID does match\n");
						if (iEnableAutoChannelMsg == 1)
						{
							printf("NowPlaying.mainloop: auto channel message enabled\n");
							ts3Functions.getChannelOfClient(result[i], myID, &resultChannelID);

							if (FormatTitle(chMessage, 2048, chChannelMsg, info) == 1)
							{
								printf("NowPlaying.mainloop: sending text to channel: %s\n", chMessage);
								ts3Functions.requestSendChannelTextMsg(result[i], chMessage, resultChannelID, 0);
							}
						}

						i++;
					} // while (result[i])
					ts3Functions.freeMemory(result);
				} // if (ts3Functions.getServerConnectionHandlerList (&result) = error_OK)
			}
		}
		else if (iFoundNothingLastTime == 0)
		{
			/* we don't need to reset applications to nothing every half second */
			iFoundNothingLastTime = 1;

			i = 0;
			memset(&lastInfo, 0, sizeof(struct TrackInfo));
			if (iEnableApplications == 1)
			{
				char command[COMMAND_BUFSIZE];
				anyID myID;

				if (ts3Functions.getServerConnectionHandlerList(&result) == ERROR_ok)
				{
					while (result[i])
					{

						if (ts3Functions.getClientID(result[i], &myID) == ERROR_ok)
						{
							snprintf(command, sizeof(command), "notPlaying?%d", myID);
							ts3Functions.sendPluginCommand(result[i], pluginID, command, PluginCommandTarget_CURRENT_CHANNEL_SUBSCRIBED_CLIENTS, NULL, NULL);
						}
						i++;
						continue;
					}
					ts3Functions.freeMemory(result);
				}
			}
		}

		LeaveCriticalSection(&cs);
		CheckIfAloneOnServers();
		Sleep(iRefreshRate);
	}
	iIsRunning = 0;
	return 0;
}
int FormatTitle(char chDestination[], unsigned int size, char *chFormat, struct TrackInfo info)
{
	char *chOccurrence = NULL;
	unsigned int iSize = 0;

	chOccurrence = strstr(chFormat, "{title}");
	if (chOccurrence != NULL && size > strlen(chFormat))
	{
		iSize = (unsigned int)(chOccurrence - chFormat);

		if (iSize != 0)
		{
			memcpy(chDestination, chFormat, iSize);
		}
		chDestination[iSize] = '\0';

		strcat_s(chDestination, size, info.chTitle);
		strcat_s(chDestination, size, chOccurrence + 7);

		return 1;
	}
	else
	{
		return 0;
	}
}
int GetRowForUser_Server(uint64 serverConnectionHandlerID, anyID clientID, int* result)
{
	unsigned int checkRec = 0;

	if (activePlayers.size() > 0)
	{
		while (checkRec < activePlayers.size())
		{
			int currentRecClientID = activePlayers.at(checkRec);
			if (activePlayers.at(checkRec) == clientID && activeServerHandlers.at(checkRec) == serverConnectionHandlerID)
			{
				printf("NowPlaying: Found existing data in row %d\n", checkRec);
				printf("\tsought playerID: %d\tserverHandlerID: %d\n", clientID, serverConnectionHandlerID);
				printf("\tfound playerID: %d\tserverHandlerID: %d\n\n", activePlayers.at(checkRec), activeServerHandlers.at(checkRec));
				*result = checkRec;
				return 1;
			}
			else
			{
				checkRec++;
				continue;
			}
		}
	}
	return 0;
}
int removeRecord(uint64 serverConnectionHandlerID, anyID clientID)
{
	int existingRecord = -1, recordRemoved = 0;
	if (GetRowForUser_Server(serverConnectionHandlerID, clientID, &existingRecord) == 1)
	{
		activePlayers.erase(activePlayers.begin() + existingRecord);
		activeServerHandlers.erase(activeServerHandlers.begin() + existingRecord);
		activeSongs.erase(activeSongs.begin() + existingRecord);
		activeUpdateTime.erase(activeUpdateTime.begin() + existingRecord);
		recordRemoved = 1;
	}
	return recordRemoved;
}
void processChannelSubOrMeMoved(uint64 serverConnectionHandlerID, uint64 channelID)
{
	anyID myID;
	anyID* toIDs;

	printf("NowPlaying: Sending song request to newly subscribed/entered channel.\n");
	if (ts3Functions.getClientID(serverConnectionHandlerID, &myID) != ERROR_ok)
	{
		printf("\tUnable to get my client ID.\n");
		return;
	}
	if (!pluginID)
	{
		printf("\tUnable to process, plugin not registered.\n");
		return;
	}

	if (ts3Functions.getChannelClientList(serverConnectionHandlerID, channelID, &toIDs) != ERROR_ok)
	{
		printf("\tFailed to get channel client list.\n");
		return;
	}
	if (toIDs[0])
	{ // channels has clients
		anyID filteredToIDs[sizeof toIDs];
		int currentElement = 0;
		int foundMe = 0;
		char returnCode[RETURNCODE_BUFSIZE];
		char command[COMMAND_BUFSIZE];

		memset(filteredToIDs, 0, sizeof toIDs);
		// need to add code to remove own ID from list if it is there.
		//printf("\tScanning for myself in the channel.\n");
		while (toIDs[currentElement])
		{
			if (myID == toIDs[currentElement])
			{
				//printf("\tI found me!!\n");
				foundMe = 1;
			}
			currentElement++;
		}
#ifdef ENABLE_QUEUED_SUB_CODE
		int iCurrentRec = 0;
		do
		{
			PlayersToRequestPlayingInfoFrom[iSlotForNextClientID] = filteredToIDs[iCurrentRec];
			iSlotForNextClientID++;
			iCurrentRec++;
		} while (filteredToIDs[iCurrentRec]);
#else
		ts3Functions.createReturnCode(pluginID, returnCode, RETURNCODE_BUFSIZE);
		sendPluginCommandReturnCodes.push_back(returnCode);
		snprintf(command, sizeof(command), "requestSong?%d", myID);

		if (foundMe)
		{
			ts3Functions.sendPluginCommand(serverConnectionHandlerID, pluginID, command, PluginCommandTarget_CURRENT_CHANNEL, NULL, returnCode);
		}
		else
		{
			ts3Functions.sendPluginCommand(serverConnectionHandlerID, pluginID, command, PluginCommandTarget_CLIENT, toIDs, returnCode);
		}
		printf("\tSent song request sent to channel (%d).\n", channelID);
#endif 
	}
	ts3Functions.freeMemory(toIDs);
}
void processClientMoveKickVisCheck(uint64 serverConnectionHandlerID, anyID clientID, int visibility)
{
	anyID myID;

	printf("NowPlaying: Processing client move/kick visiblity check.\n");
	if (!pluginID)
	{
		printf("\tUnable to process, plugin not registered.\n");
		return;
	}
	if (ts3Functions.getClientID(serverConnectionHandlerID, &myID) != ERROR_ok)
	{
		printf("\tFailed to get my ID.\n");
		return;
	}
	if (myID == clientID)
	{
		uint64 myChannelID;

		printf("\tI moved, need to ask the new channel.\n");
		if (ts3Functions.getChannelOfClient(serverConnectionHandlerID, myID, &myChannelID) == ERROR_ok)
		{
			processChannelSubOrMeMoved(serverConnectionHandlerID, myChannelID);
		}
	}
	else
	{
		printf("\tIt was not I that moved.\n");
		switch (visibility)
		{
		case ENTER_VISIBILITY:
		{
			anyID toIDs[2];
			char command[COMMAND_BUFSIZE];
			char returnCode[RETURNCODE_BUFSIZE];

			toIDs[0] = clientID;
			toIDs[1] = 0;
			snprintf(command, sizeof(command), "requestSong?%d", myID);
			ts3Functions.createReturnCode(pluginID, returnCode, RETURNCODE_BUFSIZE);
			sendPluginCommandReturnCodes.push_back(returnCode);
			ts3Functions.sendPluginCommand(serverConnectionHandlerID, pluginID, command, PluginCommandTarget_CLIENT, toIDs, returnCode);
		}
		case RETAIN_VISIBILITY:
			printf("\tNo change to visibility.\n", clientID);
			return;
		case LEAVE_VISIBILITY:
			removeRecord(serverConnectionHandlerID, clientID);
			printf("\tRecord removed, client now out of view.\n");
			return;
		}
	}

	if (myID == clientID)
	{
		printf("\tSong request sent to my new channel.\n");
	}
	else
	{
		printf("\tSong request sent to client (%d).\n", clientID);
	}
}
void removeAllServerHandlerRecords(uint64 serverConnectionHandlerID, int* removedRecords)
{
	unsigned int currentRecord = 0;

	printf("NowPlaying: Entering removeAllServerHandlerRecords Process.\n");
	//printf("\tServer handler to remove: %d.\n", serverConnectionHandlerID);
	while (currentRecord < activeServerHandlers.size())
	{
		if (activeServerHandlers[currentRecord] == serverConnectionHandlerID
			&& removeRecord(serverConnectionHandlerID, activePlayers[currentRecord]) == 1)
		{
			*removedRecords++;
		}
		else
		{
			currentRecord++;
		}
		continue;
	}
}

#ifdef ENABLE_QUEUED_SUB_CODE
void processQueuedSongRequest()
{
	if (iSlotForNextClientID)
	{
		char returnCode[RETURNCODE_BUFSIZE];
		char command[COMMAND_BUFSIZE];
		int iOnQueueRecord = 0;
		anyID queueProcessing[5000];
		anyID myID;

		if (ts3Functions.getClientID(serverConnectionHandlerID, &myID) != ERROR_ok)
		{
			printf("\tFailed to get my ID.\n");
			return;
		}
		do
		{
			queueProcessing[iOnQueueRecord] = PlayersToRequestPlayingInfoFrom[iOnQueueRecord];
			iSlotForNextClientID++;
			iOnQueueRecord++;
		} while (iOnQueueRecord < iSlotForNextClientID);
		iSlotForNextClientID = 0;
		queueProcessing[iOnQueueRecord] = 0;

		// OK I was not alone in the channel
		ts3Functions.createReturnCode(pluginID, returnCode, RETURNCODE_BUFSIZE);
		sendPluginCommandReturnCodes.push_back(returnCode);
		snprintf(command, sizeof(command), "requestSong?%d", myID);
		ts3Functions.sendPluginCommand(serverConnectionHandlerID, pluginID, command, PluginCommandTarget_CLIENT, queueProcessing, returnCode);
		printf("\tSent song request sent to channel (%d).\n", channelID);
	}
}
#endif

void togglePlayer(uint64 playerAppID)
{
	int iOK = 1;
	char cFileSetting[20] = "", cMessage[200] = "", cAppCommonName[20] = "", cOnOff[4] = "";
	int iNewValue = 0;

	switch (playerAppID)
	{
	case PLAYER_APP_WINAMP:
		if (iScanWinamp)
		{
			iNewValue = 0;
		}
		else
		{
			iNewValue = 1;
		}
		iScanWinamp = iNewValue;
		snprintf(cFileSetting, sizeof(cFileSetting), "scan_winamp");
		snprintf(cAppCommonName, sizeof(cAppCommonName), "WinAmp");
		break;
	case PLAYER_APP_VLC:
		if (iScanVlc)
		{
			iNewValue = 0;
		}
		else
		{
			iNewValue = 1;
		}
		iScanVlc = iNewValue;
		snprintf(cFileSetting, sizeof(cFileSetting), "scan_vlc");
		snprintf(cAppCommonName, sizeof(cAppCommonName), "VLC");
		break;
	case PLAYER_APP_LIGHTALLOY:
		if (iScanLightalloy)
		{
			iNewValue = 0;
		}
		else
		{
			iNewValue = 1;
		}
		iScanLightalloy = iNewValue;
		snprintf(cFileSetting, sizeof(cFileSetting), "scan_lightalloy");
		snprintf(cAppCommonName, sizeof(cAppCommonName), "Light Alloy");
		break;
	case PLAYER_APP_GOOGLE:
		if (iScanGoogle)
		{
			iNewValue = 0;
		}
		else
		{
			iNewValue = 1;
		}
		iScanGoogle = iNewValue;
		snprintf(cFileSetting, sizeof(cFileSetting), "scan_google");
		snprintf(cAppCommonName, sizeof(cAppCommonName), "Google Play");
		break;
	case PLAYER_APP_SPOTIFY:
		if (iScanSpotify)
		{
			iNewValue = 0;
		}
		else
		{
			iNewValue = 1;
		}
		iScanSpotify = iNewValue;
		snprintf(cFileSetting, sizeof(cFileSetting), "scan_spotify");
		snprintf(cAppCommonName, sizeof(cAppCommonName), "Spotify");
		break;
	case PLAYER_APP_SPIDER:
		if (iScanSpider)
		{
			iNewValue = 0;
		}
		else
		{
			iNewValue = 1;
		}
		iScanSpider = iNewValue;
		snprintf(cFileSetting, sizeof(cFileSetting), "scan_spider");
		snprintf(cAppCommonName, sizeof(cAppCommonName), "Spider Player");
		break;
	case PLAYER_APP_WMP:
		if (iScanWmp)
		{
			iNewValue = 0;
		}
		else
		{
			iNewValue = 1;
		}
		iScanWmp = iNewValue;
		snprintf(cFileSetting, sizeof(cFileSetting), "scan_wmp");
		snprintf(cAppCommonName, sizeof(cAppCommonName), "WMP");
		break;
	default:
		iOK = 0;
	}
	if (iOK)
	{
		char chFilePath[500];
		char chConfigPath[450];

		ts3Functions.getConfigPath(chConfigPath, sizeof(chConfigPath));
		snprintf(chFilePath, 500, "%s%s", chConfigPath, CONFIG_FILE);


		if (iNewValue)
		{
			snprintf(cOnOff, sizeof(cOnOff), "on");
			WritePrivateProfileString("config", cFileSetting, "1", chFilePath);
		}
		else
		{
			snprintf(cOnOff, sizeof(cOnOff), "off");
			WritePrivateProfileString("config", cFileSetting, "0", chFilePath);
		}
		snprintf(cMessage, sizeof(cMessage), "Turned %s support %s.", cAppCommonName, cOnOff);
		WritePrivateProfileString(0, 0, 0, chFilePath);
		ts3Functions.printMessageToCurrentTab(cMessage);
	}
}
void displayCurrentPlayerStatus()
{
	int isNotScanning = 1;
	if (iScanWinamp == 1)
	{
		if (isNotScanning) isNotScanning = 0;
		ts3Functions.printMessageToCurrentTab("Currently scanning for Winamp and Winamp like players.");
	}
	if (iScanVlc == 1)
	{
		if (isNotScanning) isNotScanning = 0;
		ts3Functions.printMessageToCurrentTab("Currently scanning for VLC Player.");
	}
	if (iScanLightalloy == 1)
	{
		if (isNotScanning) isNotScanning = 0;
		ts3Functions.printMessageToCurrentTab("Currently scanning for Light Alloy Player.");
	}
	if (iScanGoogle == 1)
	{
		if (isNotScanning) isNotScanning = 0;
		ts3Functions.printMessageToCurrentTab("Currently scanning for Google Play.");
	}
	if (iScanSpotify == 1)
	{
		if (isNotScanning) isNotScanning = 0;
		ts3Functions.printMessageToCurrentTab("Currently scanning for Spotify Player.");
	}
	if (iScanSpider == 1)
	{
		if (isNotScanning) isNotScanning = 0;
		ts3Functions.printMessageToCurrentTab("Currently scanning for Spider Player.");
	}
	if (iScanWmp == 1)
	{
		if (isNotScanning) isNotScanning = 0;
		ts3Functions.printMessageToCurrentTab("Currently scanning for WMP.");
	}
	if (isNotScanning)
	{
		ts3Functions.printMessageToCurrentTab("Currently not scanning for music players.");
	}
}
void spamChannel(uint64 serverConnectionHandlerID, uint64 channelID)
{
	spamTarget(serverConnectionHandlerID, channelID, 0);
}

void spamTarget(uint64 serverConnectionHandlerID, uint64 targetID, int targetIsClient)
{
	char chMessage[2048];
	char *chUniqueID;
	EnterCriticalSection(&cs);

	/*if (iFoundNothingLastTime == 1)
	{
		return;
	}*/

	/* XXX Error checking */


	if (FormatTitle(chMessage, 2048, chChannelMsg, lastInfo) == 1)
	{
		if (targetIsClient)
		{
			anyID  toID;
			toID = (int)targetID;
			ts3Functions.requestSendPrivateTextMsg(serverConnectionHandlerID, chMessage, toID, 0);
		}
		else
		{
			ts3Functions.requestSendChannelTextMsg(serverConnectionHandlerID, chMessage, targetID, 0);
		}
	}

	ts3Functions.getClientSelfVariableAsString(serverConnectionHandlerID, CLIENT_UNIQUE_IDENTIFIER, &chUniqueID);

	ts3Functions.printMessageToCurrentTab(chUniqueID);

	ts3Functions.freeMemory(chUniqueID);

	LeaveCriticalSection(&cs);
}

void CheckIfAloneOnServers()
{
	if (iLonelyMute == 1)
	{
		printf("CHECKING IF ALONE ON SERVERS\n");
		uint64 *ServerConnestionHandlerIDs;
		if (ts3Functions.getServerConnectionHandlerList(&ServerConnestionHandlerIDs) == ERROR_ok)
		{
			int i = 0;
			while (ServerConnestionHandlerIDs[i])
			{
				CheckIfAlone(ServerConnestionHandlerIDs[i]);
				i++;
				continue;
			}
			ts3Functions.freeMemory(ServerConnestionHandlerIDs);
		}
	}
}

void CheckIfAlone(uint64 serverConnectionHandlerID)
{
	if (iLonelyMute == 1)
	{
		// idea is that this can save bandwidth on the bot and server, when no one is in the channel just mute
		anyID myID;
		char *chUniqueID;

		printf("PLUGIN: NowPlaying: Checking if I'm alone.\n");
		if (ts3Functions.getClientID(serverConnectionHandlerID, &myID) != ERROR_ok)
		{
			printf("\tUnable to get my client ID.\n");
			return;
		}
		//printf("\tMy ID is %d\n", myID);

		if (strlen(chBoundToUniqueID) == 28)
		{
			//printf("\tMy bot UID is %s\n", chBoundToUniqueID);
			ts3Functions.getClientSelfVariableAsString(serverConnectionHandlerID, CLIENT_UNIQUE_IDENTIFIER, &chUniqueID);
			if (strcmp(chUniqueID, chBoundToUniqueID) == 0)
			{ /* Unique ID does match */
				uint64 resultChannelID = 0;
				anyID* clients;

				//printf("\tI am the bot!!\n");
				ts3Functions.getChannelOfClient(serverConnectionHandlerID, myID, &resultChannelID);
				//printf("\tI am in channel id %d\n", resultChannelID);
				if (ts3Functions.getChannelClientList(serverConnectionHandlerID, resultChannelID, &clients) == ERROR_ok)
				{
					int iAmMuted = 0;
					int iShouldBeMuted = 1;
					int i = 0;
					int iClientIsMuted = 0;

					ts3Functions.getClientSelfVariableAsInt(serverConnectionHandlerID, CLIENT_INPUT_MUTED, &iAmMuted);
					while (clients[i])
					{
						printf("\tLooking at client #%d in the channel with CID of %d\n", i, clients[i]);
						if (clients[i] == myID)
						{
							printf("\tThis client is me, ignore myself.\n");
							i++;
							continue;
						}
						else
						{
							if (ts3Functions.getClientVariableAsInt(serverConnectionHandlerID, clients[i], CLIENT_OUTPUT_MUTED, &iClientIsMuted) != ERROR_ok)
							{
								printf("\tError getting client is muted info.\n");
								i++;
								continue;
							}
							if (iClientIsMuted == MUTEOUTPUT_MUTED)
							{
								printf("\tClient is muted, move on to checking any other clients in the channel.\n");
								i++;
								continue;
							}
							if (ts3Functions.getClientVariableAsInt(serverConnectionHandlerID, clients[i], CLIENT_OUTPUT_HARDWARE, &iClientIsMuted) != ERROR_ok)
							{
								printf("\tError getting client output hardware info.\n");
								i++;
								continue;
							}
							if (iClientIsMuted == HARDWAREOUTPUT_DISABLED)
							{
								printf("\tClient's output hardware is disabled, move on to checking any other clients in the channel.\n");
								i++;
								continue;
							}
							printf("\tOK, I now know I should not be muted.\n");
							iShouldBeMuted = 0;
							break;
						}
					}
					printf("\tChecking if my current state is as it should be....");
					if (iAmMuted != iShouldBeMuted)
					{
						printf("need to toggle mute.\n");
						ts3Functions.setClientSelfVariableAsInt(serverConnectionHandlerID, CLIENT_INPUT_MUTED, iShouldBeMuted);
						if (ts3Functions.flushClientSelfUpdates(serverConnectionHandlerID, NULL) != ERROR_ok)
						{
							printf("\tError updating my muted state.\n");
						}
						if (iShouldBeMuted == 0 && iEnableAutoChannelMsg == 1)
						{
							spamChannel(serverConnectionHandlerID, resultChannelID);
						}
					}
					else
					{
						printf("all good.\n");
					}
					ts3Functions.freeMemory(clients);
				}
			}
			else
			{
				// UID does not match bound UID
				//printf("\tNot the bot's UID in this tab.\n");
			}
			ts3Functions.freeMemory(chUniqueID);
		}
		else
		{
			// must have bound UID configured, since this would require an update to the setting file and a plugin restart turn this feature off
			iLonelyMute = 0;
		}
	} // mute_when_alone is not enabled don't waste time checking current channel status
	//printf("PLUGIN: NowPlaying; exiting CheckIfAlone function.\n");
}

