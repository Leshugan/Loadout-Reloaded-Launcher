#pragma once
// Launcher UI shared by the launcher window and the in-game overlay.
#include <windows.h>
#include <d3d11.h>
#include <string>
#include <functional>

struct UiHost {
    bool overlay = false;
    std::function<bool(const std::string&)> writeMap;   // true = written into the game
    std::function<std::string()> currentMap;            // map currently set in the game
    std::function<int()> gameState;                     // GameState
    std::function<void()> returnToGame;
    std::function<void()> launchGame;
    std::wstring gameFolder;
};
extern UiHost g_host;
extern float S;                 // UI scale
extern bool g_uiCaptureHotkey;  // settings dialog waits for a key combination

void UiInit(ID3D11Device* dev, ID3D11DeviceContext* ctx, HMODULE resModule, float scale);
void UiSetScale(float scale);   // rebuilds style
void UiDraw();
void UiHotkeyCaptured(int mods, int vk);
void UiOnOpen();                // overlay opened: refresh state
std::string FetchMediafireDirect(const char* pageUrl);  // fresh direct link from a MediaFire page (empty on failure)
