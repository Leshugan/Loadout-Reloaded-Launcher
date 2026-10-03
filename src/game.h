#pragma once
// Game process control: launch through embedded SmartSteamEmu, patch memory like the Loadout Reloaded patcher.
#include <windows.h>
#include <string>
#include <mutex>
#include <atomic>

#include "settings.h"

extern std::atomic<int> g_gameState;
extern std::wstring g_exeDir;     // folder of launcher (= game folder)
extern std::wstring g_gameExe;    // full path of Loadout.exe
extern HWND g_mainHwnd;

void LogF(const char* fmt, ...);
void LogInit();

void GameInit();                         // detect exe, start worker thread
int GameApplyPatch();                   // uncensored patch on/off by settings: 0 done, 1 later (game running), 2 error
extern std::atomic<bool> g_hiddenStart;     // launcher window was never shown ("straight into the game")
void GameRequestLaunch();                // start game via SSE (async)
bool GameWriteMapNow(const std::string& code);   // synchronous write if ready
void GameReturnToGame();                 // restore and focus game window
void GameShutdown();
std::string GameLastWrittenMap();
DWORD GamePid();

void BringSelfToFront();
