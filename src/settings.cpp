#include "settings.h"
#include <algorithm>
#include <cstdlib>
#include <shlobj.h>

Settings g_set;
std::mutex g_setMu;
std::wstring g_iniPath;
static const char* DEFAULT_SERVER = "api.loadout.rip";

std::string W2U(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
std::wstring U2W(const std::string& s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// ------------------------------------------------------------------ settings (ini next to exe)
static const std::wstring& IniPath() { return g_iniPath; }

static std::string IniGet(const wchar_t* key, const char* def)
{
    wchar_t buf[512];
    GetPrivateProfileStringW(L"Launcher", key, U2W(def).c_str(), buf, 512, IniPath().c_str());
    return W2U(buf);
}
static void IniPut(const wchar_t* key, const std::string& v)
{
    WritePrivateProfileStringW(L"Launcher", key, U2W(v).c_str(), IniPath().c_str());
}

std::string ValidServer(const std::string& s)
{
    // Same rule as the patcher: exactly two dots, length 4..50
    int dots = (int)std::count(s.begin(), s.end(), '.');
    if (s.empty() || dots != 2 || s.size() < 4 || s.size() > 50) return DEFAULT_SERVER;
    return s;
}

static int DefaultLang()
{
    LANGID l = GetUserDefaultUILanguage();
    WORD p = PRIMARYLANGID(l);
    if (p == LANG_RUSSIAN || p == LANG_UKRAINIAN || p == LANG_BELARUSIAN) return 0;
    if (p == LANG_SPANISH) return 2;
    return 1;
}

void SettingsLoad()
{
    std::lock_guard<std::mutex> lk(g_setMu);
    std::string lang = IniGet(L"Language", "");
    if (lang == "ru") g_set.lang = 0; else if (lang == "en") g_set.lang = 1; else if (lang == "es") g_set.lang = 2; else g_set.lang = DefaultLang();
    g_set.server = IniGet(L"Server", DEFAULT_SERVER);
    g_set.startMap = IniGet(L"StartMap", "");
    g_set.customMap = IniGet(L"CustomMap", "");
    g_set.fav[0] = IniGet(L"Favorite1", "");
    g_set.fav[1] = IniGet(L"Favorite2", "");
    g_set.fav[2] = IniGet(L"Favorite3", "");
    g_set.muteMinimized = IniGet(L"MuteWhenMinimized", "0") == "1";
    g_set.uncensored = IniGet(L"UncensoredPatch", "1") == "1";
    g_set.overlayEnabled = IniGet(L"OverlayEnabled", "0") == "1";
    g_set.overlayMods = atoi(IniGet(L"OverlayMods", "4").c_str());
    g_set.overlayVk = atoi(IniGet(L"OverlayKey", "9").c_str());
    if (g_set.overlayVk <= 0 || g_set.overlayVk > 254) g_set.overlayVk = VK_TAB;
}

void SettingsSave()
{
    std::lock_guard<std::mutex> lk(g_setMu);
    std::wstring p = IniPath();
    if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) {
        // create as UTF-16 so any text is stored correctly
        HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) { DWORD w; const unsigned char bom[2] = {0xFF, 0xFE}; WriteFile(h, bom, 2, &w, nullptr); CloseHandle(h); }
    }
    static const char* L[] = {"ru", "en", "es"};
    IniPut(L"Language", L[g_set.lang]);
    IniPut(L"Server", g_set.server);
    IniPut(L"StartMap", g_set.startMap);
    IniPut(L"CustomMap", g_set.customMap);
    IniPut(L"Favorite1", g_set.fav[0]);
    IniPut(L"Favorite2", g_set.fav[1]);
    IniPut(L"Favorite3", g_set.fav[2]);
    IniPut(L"MuteWhenMinimized", g_set.muteMinimized ? "1" : "0");
    IniPut(L"UncensoredPatch", g_set.uncensored ? "1" : "0");
    IniPut(L"OverlayEnabled", g_set.overlayEnabled ? "1" : "0");
    IniPut(L"OverlayMods", std::to_string(g_set.overlayMods));
    IniPut(L"OverlayKey", std::to_string(g_set.overlayVk));
}

std::string HotkeyName(int mods, int vk)
{
    std::string s;
    if (mods & 1) s += "Ctrl + ";
    if (mods & 2) s += "Alt + ";
    if (mods & 4) s += "Shift + ";
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LONG lp = (LONG)(sc << 16);
    switch (vk) { case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
                  case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: lp |= 1 << 24; }
    wchar_t buf[64] = L"";
    if (GetKeyNameTextW(lp, buf, 64) > 0) s += W2U(buf);
    else { char t[16]; snprintf(t, sizeof(t), "#%d", vk); s += t; }
    return s;
}

std::wstring g_dataRoot;

std::wstring AppDataDir()
{
    std::wstring d;
    if (!g_dataRoot.empty()) d = g_dataRoot + L"\\LoadoutLauncher_Data";   // inside the game folder
    else {
        wchar_t* p = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) { d = p; CoTaskMemFree(p); }
        d += L"\\LoadoutLauncher";
    }
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}
