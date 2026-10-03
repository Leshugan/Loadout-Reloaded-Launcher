#pragma once
// Settings shared by the launcher and the in-game overlay (same LoadoutLauncher.ini next to Loadout.exe).
#include <windows.h>
#include <string>
#include <mutex>

enum GameState { GS_NOEXE = 0, GS_IDLE, GS_LAUNCHING, GS_WAIT_INIT, GS_READY, GS_CLOSED, GS_FAILED };

struct Settings {
    int lang = 1;
    std::string server = "api.loadout.rip";
    std::string startMap;
    std::string customMap;
    std::string fav[3];
    bool muteMinimized = false;
    bool uncensored = true;
    int launchMode = 0;              // 0: choose first, then "Launch game"; 1: game starts at once with the launcher on top of it          // Loadout Reloaded "Uncensored patch" (Data\\29911B90.ARC)
    bool overlayEnabled = false;
    int overlayMods = 4;  // 1 ctrl, 2 alt, 4 shift
    int overlayVk = VK_TAB;
};
extern Settings g_set;
extern std::mutex g_setMu;
extern std::wstring g_iniPath;
void SettingsLoad();
void SettingsSave();
std::string ValidServer(const std::string& s); // returns server or default
std::string HotkeyName(int mods, int vk);
extern std::wstring g_dataRoot;   // game folder (logs and emulator files go to <game>\\LoadoutLauncher_Data)
std::wstring AppDataDir();
std::string W2U(const std::wstring& w);
std::wstring U2W(const std::string& s);
