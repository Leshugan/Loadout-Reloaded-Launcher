#include "game.h"
#include <tlhelp32.h>
#include <shlobj.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdarg>
#include <thread>
#include <vector>
#include <algorithm>
#include <map>
#include "miniz.h"

std::atomic<int> g_gameState{GS_IDLE};
std::atomic<bool> g_hiddenStart{false};
std::wstring g_exeDir, g_gameExe;
HWND g_mainHwnd = nullptr;

static const char* DEFAULT_SERVER = "api.loadout.rip";
static const char* DEFAULT_MAP = "shooting_gallery_solo";
static const char* UBERENT_EP = "uberent.com";
// Addresses from the original patcher (Loadout.exe has no ASLR)
static const uintptr_t ADDR_UBERENT = 0x1015434;
static const uintptr_t ADDR_UES = 0x0f438b8;
static const uintptr_t ADDR_MM = 0x1015540;
static const uintptr_t ADDR_MAP = 0x0cc94d0;

// ------------------------------------------------------------------ log
static std::mutex g_logMu;
static FILE* g_log = nullptr;

void LogInit()
{
    std::wstring p = AppDataDir() + L"\\LoadoutLauncher.log";
    g_log = _wfopen(p.c_str(), L"w");
}

void LogF(const char* fmt, ...)
{
    std::lock_guard<std::mutex> lk(g_logMu);
    if (!g_log) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

// ------------------------------------------------------------------ process helpers
static DWORD FindGamePid()
{
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W e{}; e.dwSize = sizeof(e);
    if (Process32FirstW(snap, &e)) {
        do {
            if (_wcsicmp(e.szExeFile, L"Loadout.exe") == 0) { pid = e.th32ProcessID; break; }
        } while (Process32NextW(snap, &e));
    }
    CloseHandle(snap);
    return pid;
}

struct FindWnd { DWORD pid; HWND best; int area; };
static BOOL CALLBACK EnumWndProc(HWND h, LPARAM lp)
{
    FindWnd* f = (FindWnd*)lp;
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (pid != f->pid || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    RECT r; GetWindowRect(h, &r);
    int a = (r.right - r.left) * (r.bottom - r.top);
    if (IsIconic(h)) a = 1000000000; // minimized main window still counts
    if (a > f->area) { f->area = a; f->best = h; }
    return TRUE;
}
static HWND FindGameWindow(DWORD pid)
{
    if (!pid) return nullptr;
    FindWnd f{pid, nullptr, 0};
    EnumWindows(EnumWndProc, (LPARAM)&f);
    return f.best;
}

static void ForceForeground(HWND h)
{
    // the TrueAltTab / NoFocusSteal way: borrow the input of the active window, no Alt press
    if (!h) return;
    if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
    HWND fg = GetForegroundWindow();
    DWORD fgT = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD me = GetCurrentThreadId();
    bool attached = fgT && fgT != me && AttachThreadInput(me, fgT, TRUE);
    BringWindowToTop(h);
    SetForegroundWindow(h);
    if (attached) AttachThreadInput(me, fgT, FALSE);
    if (GetForegroundWindow() == h) return;
    // fallback: a key no program uses unlocks SetForegroundWindow
    INPUT in[2] = {};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = 0xE8;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
    SetForegroundWindow(h);
}

// ------------------------------------------------------------------ embedded SmartSteamEmu
struct EmbFile { const wchar_t* res; const wchar_t* path; };
static const EmbFile SSE_FILES[] = {
    { L"SSE_DLL", L"SmartSteamEmu.dll" },
    { L"SSE_PLG_LOADOUT", L"SmartSteamEmu\\Plugins\\SSELoadout.dll" },
    { L"SSE_PLG_FW", L"SmartSteamEmu\\Plugins\\SSEFirewall.dll" },
    { L"SSE_PLG_FWINI", L"SmartSteamEmu\\Plugins\\SSEFirewall.ini" },
    { L"SSE_PLG_OVINI", L"SmartSteamEmu\\Plugins\\SSEOverlay.ini" },
    { L"SSE_PLG_OV", L"SmartSteamEmu\\Plugins\\x86\\SSEOverlay.dll" },
    { L"SSE_OV_LANG", L"SmartSteamEmu\\Plugins\\SSEOverlay\\Language.ini" },
    { L"SSE_OV_MSG", L"SmartSteamEmu\\Plugins\\SSEOverlay\\message.wav" },
    { L"SSE_OV_SHOT", L"SmartSteamEmu\\Plugins\\SSEOverlay\\screenshot.wav" },
    { L"SSE_AVATAR", L"SmartSteamEmu\\Common\\avatar.png" },
    { L"OVERLAY_DLL", L"SmartSteamEmu\\Plugins\\LoadoutOverlay.dll" },
};

// Big resources are stored packed ("LLZ1" + size + zlib, see tools/pack.py) and unpacked here once.
static bool GetRes(const wchar_t* name, const void** data, DWORD* size)
{
    static std::mutex mu;
    static std::map<std::wstring, std::vector<unsigned char>> cache;
    HRSRC r = FindResourceW(nullptr, name, (LPCWSTR)RT_RCDATA);
    if (!r) return false;
    HGLOBAL g = LoadResource(nullptr, r);
    if (!g) return false;
    const unsigned char* p = (const unsigned char*)LockResource(g);
    DWORD n = SizeofResource(nullptr, r);
    if (!p) return false;
    if (n < 8 || memcmp(p, "LLZ1", 4) != 0) { *data = p; *size = n; return true; }
    std::lock_guard<std::mutex> lk(mu);
    auto& out = cache[name];
    if (out.empty()) {
        mz_ulong raw = p[4] | (p[5] << 8) | (p[6] << 16) | ((mz_ulong)p[7] << 24);
        out.resize(raw);
        if (mz_uncompress(out.data(), &raw, p + 8, n - 8) != MZ_OK || raw != out.size()) { out.clear(); return false; }
    }
    *data = out.data(); *size = (DWORD)out.size();
    return true;
}

static std::wstring SseDir()
{
    return AppDataDir() + L"\\SSE";
}

static void MakeDirs(const std::wstring& path)
{
    for (size_t i = 3; i < path.size(); i++)
        if (path[i] == L'\\') CreateDirectoryW(path.substr(0, i).c_str(), nullptr);
    CreateDirectoryW(path.c_str(), nullptr);
}

static bool WriteWhole(const std::wstring& path, const void* data, DWORD size)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0; BOOL ok = WriteFile(h, data, size, &w, nullptr);
    CloseHandle(h);
    return ok && w == size;
}

static bool ExtractSse(const std::wstring& dir)
{
    bool ok = true;
    for (auto& f : SSE_FILES) {
        const void* d; DWORD sz;
        if (!GetRes(f.res, &d, &sz)) { LogF("resource missing: %s", W2U(f.res).c_str()); ok = false; continue; }
        std::wstring p = dir + L"\\" + f.path;
        MakeDirs(p.substr(0, p.rfind(L'\\')));
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fa) && fa.nFileSizeHigh == 0 && fa.nFileSizeLow == sz) continue;
        if (!WriteWhole(p, d, sz)) { LogF("cannot write %s (err %lu)", W2U(p).c_str(), GetLastError()); ok = false; }
    }
    return ok;
}

static bool WriteSseIni(const std::wstring& dir, const std::wstring& iniPath, int lang)
{
    bool ov; { std::lock_guard<std::mutex> lk(g_setMu); ov = g_set.overlayEnabled; }
    static const wchar_t* LN[] = {L"Russian", L"English", L"Spanish"};
    std::wstring s;
    s += L"# Generated by Loadout Launcher\r\n\r\n";
    s += L"[SmartSteamEmu]\r\nAvatarFilename = avatar.png\r\nPersonaName = AccountName\r\nAppId = 208090\r\n";
    s += L"SteamIdGeneration = GenerateRandom\r\nManualSteamId = 0\r\n";
    s += std::wstring(L"Language = ") + LN[lang] + L"\r\n";
    s += L"LowViolence = False\r\nStorageOnAppdata = True\r\nSeparateStorageByName = False\r\nAutomaticallyJoinInvite = True\r\n";
    s += L"EnableHTTP = False\r\nEnableInGameVoice = False\r\nEnableLobbyFilter = True\r\nDisableFriendList = False\r\n";
    s += L"DisableLeaderboard = False\r\nDisableGC = False\r\nSecuredServer = True\r\nVR = False\r\nOffline = False\r\n";
    s += ov ? L"QuickJoinHotkey = CTRL + SHIFT + F11\r\n" : L"QuickJoinHotkey = SHIFT + TAB\r\n";
    s += L"MasterServer = 188.40.40.201:27010 \r\nMasterServerGoldSrc = 188.40.40.201:27010 \r\n\r\n";
    s += L"[Achievements]\r\nFailOnNonExistenceStats = False\r\n\r\n";
    s += ov ? L"[SSEOverlay]\r\nDisableOverlay = True" : L"[SSEOverlay]\r\nDisableOverlay = False";
    s += L"\r\nOnlineMode = True\r\nLanguage = \r\nScreenshotHotkey = F12\r\nHookRefCount = True\r\nOnlineKey = \r\n\r\n";
    s += L"[DirectPatch]\r\n\r\n";
    s += L"[Debug]\r\nEnableLog = False\r\nMarkLogHotkey = CTRL + ALT + M\r\nLogFilter = User Logged On\r\nMinidump = True\r\n\r\n";
    s += L"[DLC]\r\nDefault = True\r\n\r\n";
    s += L"[Networking]\r\nBroadcastAddress = 255.255.255.255 \r\nListenPort = 31313\r\nMaximumPort = 10\r\nDiscoveryInterval = 3\r\nMaximumConnection = 200\r\n\r\n";
    s += L"[PlayerManagement]\r\nAllowAnyoneConnect = True\r\nAdminPassword = \r\n";
    std::string bytes = "\xFF\xFE";
    bytes.append((const char*)s.data(), s.size() * sizeof(wchar_t));
    return WriteWhole(iniPath, bytes.data(), (DWORD)bytes.size());
}

// ------------------------------------------------------------------ built-in Steam emulator start
// Does what SmartSteamLoader.exe did: tells the game's steam_api.dll where the emulator is
// (registry "ActiveProcess"), fills the environment, then starts the game.
static const wchar_t* AP_KEY = L"Software\\Valve\\Steam\\ActiveProcess";
struct RegBackup { bool have = false; DWORD pid = 0; bool hasPid = false; std::wstring dll, dll64, lang; bool hasDll = false, hasDll64 = false, hasLang = false; };
static RegBackup g_regOld;
static std::mutex g_regMu;
static std::atomic<DWORD> g_regPid{0};
static std::atomic<bool> g_regThread{false};
static HANDLE g_steamObjs[6] = {};

static bool RegGetStr(HKEY k, const wchar_t* name, std::wstring* out)
{
    wchar_t buf[1024] = {}; DWORD sz = sizeof(buf) - 2, type = 0;
    if (RegQueryValueExW(k, name, nullptr, &type, (BYTE*)buf, &sz) != ERROR_SUCCESS || type != REG_SZ) return false;
    *out = buf; return true;
}
static void RegPutStr(HKEY k, const wchar_t* name, const std::wstring& v)
{
    RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
}

static void RegApply(const std::wstring& dll, const std::wstring& dllDir, const wchar_t* lang, DWORD pid)
{
    std::lock_guard<std::mutex> lk(g_regMu);
    HKEY k;
    if (!g_regOld.have) {
        if (RegOpenKeyExW(HKEY_CURRENT_USER, AP_KEY, 0, KEY_READ, &k) == ERROR_SUCCESS) {
            DWORD sz = 4, type = 0;
            g_regOld.hasPid = RegQueryValueExW(k, L"pid", nullptr, &type, (BYTE*)&g_regOld.pid, &sz) == ERROR_SUCCESS;
            g_regOld.hasDll = RegGetStr(k, L"SteamClientDll", &g_regOld.dll);
            g_regOld.hasDll64 = RegGetStr(k, L"SteamClientDll64", &g_regOld.dll64);
            RegCloseKey(k);
        }
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &k) == ERROR_SUCCESS) {
            g_regOld.hasLang = RegGetStr(k, L"Language", &g_regOld.lang);
            RegCloseKey(k);
        }
        g_regOld.have = true;
    }
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        RegPutStr(k, L"Language", lang); RegCloseKey(k);
    }
    if (RegCreateKeyExW(HKEY_CURRENT_USER, AP_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(k, L"pid", 0, REG_DWORD, (const BYTE*)&pid, 4);
        RegPutStr(k, L"SteamClientDll", dll);
        RegPutStr(k, L"SteamClientDll64", dllDir + L"\\");
        RegCloseKey(k);
    }
}

static void RegRestore()
{
    std::lock_guard<std::mutex> lk(g_regMu);
    if (!g_regOld.have) return;
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, AP_KEY, 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
        if (g_regOld.hasPid) RegSetValueExW(k, L"pid", 0, REG_DWORD, (const BYTE*)&g_regOld.pid, 4); else RegDeleteValueW(k, L"pid");
        if (g_regOld.hasDll) RegPutStr(k, L"SteamClientDll", g_regOld.dll); else RegDeleteValueW(k, L"SteamClientDll");
        if (g_regOld.hasDll64) RegPutStr(k, L"SteamClientDll64", g_regOld.dll64); else RegDeleteValueW(k, L"SteamClientDll64");
        RegCloseKey(k);
    }
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
        if (g_regOld.hasLang) RegPutStr(k, L"Language", g_regOld.lang); else RegDeleteValueW(k, L"Language");
        RegCloseKey(k);
    }
    g_regOld = RegBackup();
    LogF("steam registry restored");
}

// Keeps "pid" pointing at the running game (a real Steam client may overwrite it).
static void RegPidThread()
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, AP_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) { g_regThread = false; return; }
    while (DWORD pid = g_regPid.load()) {
        RegSetValueExW(k, L"pid", 0, REG_DWORD, (const BYTE*)&pid, 4);
        Sleep(1000);
    }
    RegCloseKey(k);
    g_regThread = false;
}

// Objects a running Steam client normally owns.
static void SteamObjectsCreate()
{
    if (g_steamObjs[0]) return;
    g_steamObjs[0] = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 0x400, L"Local\\SteamStart_SharedMemFile");
    g_steamObjs[1] = CreateEventW(nullptr, FALSE, FALSE, L"Local\\SteamStart_SharedMemLock");
    if (g_steamObjs[1]) SetEvent(g_steamObjs[1]);
    g_steamObjs[2] = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 0x1000, L"STEAM_DRM_IPC");
    g_steamObjs[3] = CreateEventW(nullptr, FALSE, FALSE, L"Local\\Steam3Master_SharedMemLock");
    if (g_steamObjs[3]) SetEvent(g_steamObjs[3]);
    g_steamObjs[4] = CreateSemaphoreW(nullptr, 0, 512, L"STEAM_DIPC_CONSUME");
    g_steamObjs[5] = CreateSemaphoreW(nullptr, 0, 512, L"STEAM_DIPC_PRODUCE");
}

static void GameEmuStopped()      // game closed: give the registry back
{
    g_regPid = 0;
    RegRestore();
}

static bool LaunchViaSse()
{
    int lang; { std::lock_guard<std::mutex> lk(g_setMu); lang = g_set.lang; }
    static const wchar_t* LN[] = {L"Russian", L"English", L"Spanish"};
    std::wstring dir = SseDir();
    MakeDirs(dir);
    DeleteFileW((dir + L"\\SmartSteamLoader.exe").c_str());     // left by older versions
    LogF("SSE folder: %s", W2U(dir).c_str());
    if (!ExtractSse(dir)) LogF("warning: some SSE files were not extracted");
    std::wstring ini = dir + L"\\launcher.ini";
    if (!WriteSseIni(dir, ini, lang)) { LogF("cannot write %s", W2U(ini).c_str()); return false; }
    std::wstring dll = dir + L"\\SmartSteamEmu.dll";
    GameApplyPatch();

    wchar_t self[MAX_PATH] = {}; GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::vector<std::pair<std::wstring, std::wstring>> vars = {
        {L"SteamAppId", L"208090"}, {L"SteamGameId", L"208090"}, {L"SteamAppVersionId", L"0"},
        {L"SteamUser", L"AccountName"}, {L"SteamAppUser", L"AccountName"},
        {L"SteamPath", dir}, {L"ValvePlatformMutex", self},
        {L"SSEConfigPath", ini}, {L"SSEInject", L"0"}, {L"SSEStartIn", g_exeDir},
        {L"SSEPersonaName", L"AccountName"}, {L"SSELanguage", LN[lang]},
    };
    // environment block: ours without the variables above, then the variables
    std::vector<wchar_t> env;
    if (wchar_t* cur = GetEnvironmentStringsW()) {
        for (wchar_t* p = cur; *p; p += wcslen(p) + 1) {
            std::wstring e = p;
            bool skip = false;
            for (auto& v : vars) if (e.size() > v.first.size() && e[v.first.size()] == L'=' && _wcsnicmp(e.c_str(), v.first.c_str(), v.first.size()) == 0) skip = true;
            if (!skip) { env.insert(env.end(), e.begin(), e.end()); env.push_back(0); }
        }
        FreeEnvironmentStringsW(cur);
    }
    for (auto& v : vars) { std::wstring e = v.first + L"=" + v.second; env.insert(env.end(), e.begin(), e.end()); env.push_back(0); }
    env.push_back(0);

    SteamObjectsCreate();
    RegApply(dll, dir, LN[lang], GetCurrentProcessId());

    std::wstring cmd = L"\"" + g_gameExe + L"\"";
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end()); cmdBuf.push_back(0);
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(g_gameExe.c_str(), cmdBuf.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, env.data(), g_exeDir.c_str(), &si, &pi)) {
        LogF("CreateProcess game failed, err %lu", GetLastError());
        RegRestore();
        return false;
    }
    // point "pid" at the game itself, so it stays valid even if the launcher is closed
    g_regPid = pi.dwProcessId;
    { HKEY k; if (RegOpenKeyExW(HKEY_CURRENT_USER, AP_KEY, 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) { DWORD p = pi.dwProcessId; RegSetValueExW(k, L"pid", 0, REG_DWORD, (const BYTE*)&p, 4); RegCloseKey(k); } }
    if (!g_regThread.exchange(true)) std::thread(RegPidThread).detach();
    ResumeThread(pi.hThread);
    LogF("game started, pid %lu", pi.dwProcessId);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return true;
}

// ------------------------------------------------------------------ uncensored patch
// The Loadout Reloaded patch is just another Data\29911B90.ARC; both versions are inside the launcher.
int GameApplyPatch()
{
    bool want; { std::lock_guard<std::mutex> lk(g_setMu); want = g_set.uncensored; }
    std::wstring file = g_exeDir + L"\\Data\\29911B90.ARC";
    std::wstring backup = AppDataDir() + L"\\29911B90.ARC.original";     // the player's own file, kept before patching
    if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) return 2;
    const void* pdata; DWORD psize;
    if (!GetRes(L"ARC_UNCENSORED", &pdata, &psize)) return 2;
    auto ReadAll = [](const std::wstring& f, std::vector<char>& out) {
        HANDLE h = CreateFileW(f.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER sz{}; bool ok = GetFileSizeEx(h, &sz) && sz.QuadPart < (1ll << 30);
        if (ok) { out.resize((size_t)sz.QuadPart); DWORD rd = 0; ok = ReadFile(h, out.data(), (DWORD)out.size(), &rd, nullptr) && rd == out.size(); }
        CloseHandle(h);
        return ok;
    };
    std::vector<char> cur;
    if (!ReadAll(file, cur)) return 2;
    bool isPatched = cur.size() == psize && memcmp(cur.data(), pdata, psize) == 0;
    if (want == isPatched) return 0;
    if (FindGamePid()) { LogF("uncensored patch: game is running, will apply later"); return 1; }
    std::wstring tmp = file + L".new";
    bool ok;
    if (want) {
        if (!WriteWhole(backup, cur.data(), (DWORD)cur.size())) { LogF("uncensored patch: cannot save the original file"); return 2; }
        ok = WriteWhole(tmp, pdata, psize);
    } else {
        std::vector<char> orig;
        if (!ReadAll(backup, orig)) { LogF("uncensored patch: no saved original file"); return 2; }
        ok = WriteWhole(tmp, orig.data(), (DWORD)orig.size());
    }
    if (!ok || !MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        LogF("uncensored patch: cannot write %s (err %lu)", W2U(file).c_str(), GetLastError());
        return 2;
    }
    LogF("uncensored patch %s", want ? "applied" : "removed");
    return 0;
}

static std::atomic<bool> g_userReturned{false};

// ------------------------------------------------------------------ memory
static std::mutex g_procMu;
static HANDLE g_proc = nullptr;
static DWORD g_pid = 0;
static std::string g_writtenMap;
static std::string g_pendingMap;
static int g_pendingVer = 0, g_writtenVer = 0;

static std::string ReadStr(uintptr_t addr, bool* ok)
{
    char buf[72] = {};
    SIZE_T rd = 0;
    *ok = ReadProcessMemory(g_proc, (LPCVOID)addr, buf, 64, &rd) != 0;
    if (!*ok) return {};
    std::string s;
    for (int i = 0; i < 64 && buf[i]; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c < 32 || c > 126) break;
        s += (char)c;
    }
    return s;
}

static bool WriteStr(uintptr_t addr, const std::string& v)
{
    SIZE_T wr = 0;
    BOOL ok = WriteProcessMemory(g_proc, (LPVOID)addr, v.c_str(), v.size() + 1, &wr);
    if (!ok) {
        DWORD old;
        if (VirtualProtectEx(g_proc, (LPVOID)addr, v.size() + 1, PAGE_READWRITE, &old)) {
            ok = WriteProcessMemory(g_proc, (LPVOID)addr, v.c_str(), v.size() + 1, &wr);
            VirtualProtectEx(g_proc, (LPVOID)addr, v.size() + 1, old, &old);
        }
    }
    if (!ok) { LogF("write '%s' at 0x%llx failed, err %lu", v.c_str(), (unsigned long long)addr, GetLastError()); return false; }
    bool rok; std::string back = ReadStr(addr, &rok);
    if (back != v) { LogF("verify at 0x%llx: read '%s' instead of '%s'", (unsigned long long)addr, back.c_str(), v.c_str()); return false; }
    return true;
}

static bool WriteEndpoints(const std::string& server)
{
    bool a = WriteStr(ADDR_UBERENT, server);
    bool b = WriteStr(ADDR_UES, server);
    bool c = WriteStr(ADDR_MM, server);
    LogF("endpoints -> %s : %d %d %d", server.c_str(), a, b, c);
    return a && b && c;
}

static bool WriteMapLocked(const std::string& code)
{
    if (!g_proc || code.empty()) return false;
    bool ok = WriteStr(ADDR_MAP, code);
    LogF("map -> %s : %s", code.c_str(), ok ? "ok" : "FAILED");
    if (ok) g_writtenMap = code;
    return ok;
}

bool GameWriteMapNow(const std::string& code)
{
    std::lock_guard<std::mutex> lk(g_procMu);
    g_pendingMap = code; g_pendingVer++;
    if (g_gameState != GS_READY) return false;
    bool ok = WriteMapLocked(code);
    if (ok) g_writtenVer = g_pendingVer;
    return ok;
}


std::string GameLastWrittenMap() { std::lock_guard<std::mutex> lk(g_procMu); return g_writtenMap; }
DWORD GamePid() { std::lock_guard<std::mutex> lk(g_procMu); return g_pid; }

static bool GameWantsFullscreen()
{
    // %LOCALAPPDATA%\EdgeOfReality\Loadout\engine.ini : Windowed=0 means fullscreen
    wchar_t* p = nullptr; std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) { path = p; CoTaskMemFree(p); }
    path += L"\\EdgeOfReality\\Loadout\\engine.ini";
    FILE* f = _wfopen(path.c_str(), L"r");
    if (!f) return true;
    char line[256]; bool full = true;
    while (fgets(line, sizeof(line), f)) if (!strncmp(line, "Windowed=", 9)) full = atoi(line + 9) == 0;
    fclose(f);
    return full;
}

static bool GameIsFullscreen(HWND gw)
{
    QUERY_USER_NOTIFICATION_STATE st;
    if (SUCCEEDED(SHQueryUserNotificationState(&st)) && st == QUNS_RUNNING_D3D_FULL_SCREEN) return true;
    HMONITOR mon = MonitorFromWindow(gw, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{}; mi.cbSize = sizeof(mi); GetMonitorInfoW(mon, &mi);
    RECT r; GetWindowRect(gw, &r);
    LONG style = GetWindowLongW(gw, GWL_STYLE);
    return !(style & WS_CAPTION) && r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top && r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom;
}

static void SendAltEnter()
{
    keybd_event(VK_MENU, 0x38, 0, 0);
    keybd_event(VK_RETURN, 0x1C, 0, 0);
    keybd_event(VK_RETURN, 0x1C, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_MENU, 0x38, KEYEVENTF_KEYUP, 0);
}

static std::atomic<bool> g_fsBusy{false};

// Makes sure the game ends up in fullscreen (if its settings say fullscreen). Logs every step.
static void EnsureFullscreen(HWND gw, const char* why)
{
    bool want = GameWantsFullscreen();
    LogF("fullscreen check (%s): game settings say %s", why, want ? "fullscreen" : "windowed");
    if (!want || g_fsBusy.exchange(true)) return;
    std::thread([gw]() {
        int presses = 0; ULONGLONG start = GetTickCount64(), lastPress = 0;
        while (GetTickCount64() - start < 25000) {
            Sleep(700);
            if (!IsWindow(gw)) { LogF("fullscreen check: game window gone"); break; }
            if (GameIsFullscreen(gw)) { LogF("game is fullscreen"); break; }
            if (GetForegroundWindow() != gw) {
                LogF("fullscreen check: game is not in front, bringing it");
                if (IsIconic(gw)) ShowWindow(gw, SW_RESTORE);
                ForceForeground(gw);
                continue;
            }
            // in front but windowed: give the game a moment to switch by itself, then press Alt+Enter
            if (GetTickCount64() - lastPress < 3000) continue;
            if (presses >= 3) { LogF("fullscreen check: still windowed after 3 tries, giving up"); break; }
            presses++; lastPress = GetTickCount64();
            LogF("game is windowed, sending Alt+Enter (%d)", presses);
            SendAltEnter();
        }
        g_fsBusy = false;
    }).detach();
}

void GameReturnToGame()
{
    DWORD pid = GamePid();
    HWND gw = FindGameWindow(pid);
    LogF("return to game: pid %lu hwnd %p", pid, gw);
    g_userReturned = true;
    if (!gw) return;
    if (IsWindowVisible(g_mainHwnd)) ShowWindow(g_mainHwnd, SW_MINIMIZE);
    if (IsIconic(gw)) ShowWindow(gw, SW_RESTORE);
    ForceForeground(gw);
    LogF("game in front: %s", GetForegroundWindow() == gw ? "yes" : "no");
    EnsureFullscreen(gw, "return");
}


// ------------------------------------------------------------------ game sound (mute while minimized)
static bool SetProcessMuted(DWORD pid, bool mute)
{
    bool found = false;
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en))) return false;
    IMMDeviceCollection* devs = nullptr;
    if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devs))) {
        UINT nd = 0; devs->GetCount(&nd);
        for (UINT d = 0; d < nd; d++) {
            IMMDevice* dev = nullptr;
            if (FAILED(devs->Item(d, &dev))) continue;
            IAudioSessionManager2* mgr = nullptr;
            if (SUCCEEDED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)&mgr))) {
                IAudioSessionEnumerator* se = nullptr;
                if (SUCCEEDED(mgr->GetSessionEnumerator(&se))) {
                    int n = 0; se->GetCount(&n);
                    for (int i = 0; i < n; i++) {
                        IAudioSessionControl* c = nullptr;
                        if (FAILED(se->GetSession(i, &c))) continue;
                        IAudioSessionControl2* c2 = nullptr;
                        if (SUCCEEDED(c->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&c2))) {
                            DWORD sp = 0;
                            if (SUCCEEDED(c2->GetProcessId(&sp)) && sp == pid) {
                                ISimpleAudioVolume* v = nullptr;
                                if (SUCCEEDED(c->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&v))) { v->SetMute(mute, nullptr); v->Release(); found = true; }
                            }
                            c2->Release();
                        }
                        c->Release();
                    }
                    se->Release();
                }
                mgr->Release();
            }
            dev->Release();
        }
        devs->Release();
    }
    en->Release();
    return found;
}

// ------------------------------------------------------------------ worker
static std::atomic<bool> g_quit{false};
static std::atomic<bool> g_launchReq{false};
static std::thread g_worker;

static void CloseGameHandle()
{
    std::lock_guard<std::mutex> lk(g_procMu);
    if (g_proc) CloseHandle(g_proc);
    g_proc = nullptr; g_pid = 0;
    g_writtenMap.clear();
    g_writtenVer = 0;
}

static void WorkerMain()
{
    ULONGLONG launchedAt = 0, foundAt = 0, seenAt = 0, verifyUntil = 0, lastVerify = 0;
    bool launchedByUs = false;
    std::string server;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    int muted = -1;            // -1 unknown, 0 sound on, 1 muted by us
    ULONGLONG lastMute = 0;

    while (!g_quit) {
        ULONGLONG now = GetTickCount64();
        if (g_gameState == GS_NOEXE) { Sleep(200); continue; }   // first-run screen: nothing to control

        if (g_launchReq.exchange(false)) {
            if (!FindGamePid()) {
                g_gameState = GS_LAUNCHING;
                if (LaunchViaSse()) { launchedAt = now; launchedByUs = true; g_userReturned = false; if (IsWindowVisible(g_mainHwnd)) ShowWindow(g_mainHwnd, SW_MINIMIZE); }   // step aside so the game gets the screen
                else { g_gameState = GS_FAILED; if (!IsWindowVisible(g_mainHwnd)) ShowWindow(g_mainHwnd, SW_SHOW); }
            }
        }

        bool haveProc; { std::lock_guard<std::mutex> lk(g_procMu); haveProc = g_proc != nullptr; }

        if (!haveProc) {
            DWORD pid = FindGamePid();
            if (pid) {
                HANDLE h = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
                if (h) {
                    { std::lock_guard<std::mutex> lk(g_procMu); g_proc = h; g_pid = pid; }
                    g_gameState = GS_WAIT_INIT;
                    foundAt = now; seenAt = 0;
                    { std::lock_guard<std::mutex> lk(g_setMu); server = ValidServer(g_set.server); }
                    LogF("Loadout.exe found, pid %lu", pid);
                } else {
                    LogF("OpenProcess failed, err %lu (try running as administrator)", GetLastError());
                    Sleep(1000);
                }
            } else if (g_gameState == GS_LAUNCHING && now - launchedAt > 90000) {
                LogF("game process did not appear within 90 s");
                g_gameState = GS_FAILED;
                if (!IsWindowVisible(g_mainHwnd)) ShowWindow(g_mainHwnd, SW_SHOW);   // hidden start: show what went wrong
            }
        } else {
            HANDLE h; { std::lock_guard<std::mutex> lk(g_procMu); h = g_proc; }
            if (WaitForSingleObject(h, 0) == WAIT_OBJECT_0) {
                LogF("Loadout.exe closed");
                CloseGameHandle();
                GameEmuStopped();
                GameApplyPatch();      // a change made while the game was running
                muted = -1;
                g_gameState = GS_CLOSED;
                launchedByUs = false;
                if (g_hiddenStart && !IsWindowVisible(g_mainHwnd)) { LogF("game closed, hidden launcher exits"); PostMessageW(g_mainHwnd, WM_CLOSE, 0, 0); }
                Sleep(100);
                continue;
            }

            if (g_gameState == GS_WAIT_INIT) {
                std::lock_guard<std::mutex> lk(g_procMu);
                bool o1, o2, o3, o4;
                std::string a = ReadStr(ADDR_UBERENT, &o1), b = ReadStr(ADDR_UES, &o2), c = ReadStr(ADDR_MM, &o3), d = ReadStr(ADDR_MAP, &o4);
                bool all = a.size() > 3 && b.size() > 3 && c.size() > 3 && d.size() > 3;
                if (all && !seenAt) { seenAt = now; LogF("endpoints initialized: [%s] [%s] [%s] [%s]", a.c_str(), b.c_str(), c.c_str(), d.c_str()); }
                if (!all) seenAt = 0;
                bool timeout = now - foundAt > 60000;
                if ((seenAt && now - seenAt >= 1500) || timeout) {
                    if (timeout && !seenAt) LogF("endpoints still not initialized after 60 s, continuing with risk");
                    WriteEndpoints(server);
                    std::string sm; { std::lock_guard<std::mutex> l2(g_setMu); sm = g_set.startMap; }
                    if (!sm.empty() && sm != DEFAULT_MAP) WriteMapLocked(sm);
                    verifyUntil = now + 15000; lastVerify = now;
                    g_gameState = GS_READY;
                    LogF("ready");
                    if (launchedByUs) { HWND gw = FindGameWindow(g_pid); if (gw) EnsureFullscreen(gw, "ready"); }
                }
            } else if (g_gameState == GS_READY) {
                std::lock_guard<std::mutex> lk(g_procMu);
                // the game may re-initialize strings shortly after start: keep ours in place for a while
                if (now < verifyUntil && now - lastVerify >= 500) {
                    lastVerify = now;
                    bool ok; std::string a = ReadStr(ADDR_UBERENT, &ok);
                    if (ok && a != server) { LogF("endpoint was reset by the game ('%s'), writing again", a.c_str()); WriteEndpoints(server); }
                }
                if (g_pendingVer != g_writtenVer && !g_pendingMap.empty()) {
                    if (WriteMapLocked(g_pendingMap)) g_writtenVer = g_pendingVer;
                    else g_writtenVer = g_pendingVer; // don't spam; UI shows log hint
                }
            }

            // started by us: as soon as the game window shows up, put it in front (the launcher steps aside)
            if (launchedByUs && !g_userReturned) {
                DWORD pid = GamePid();
                HWND gw = FindGameWindow(pid);
                if (gw && IsWindowVisible(gw)) { LogF("game window is up, bringing it to the front"); GameReturnToGame(); }
            }

            // sound off while the game is minimized (option)
            if (now - lastMute >= 300) {
                lastMute = now;
                bool opt; { std::lock_guard<std::mutex> lk(g_setMu); opt = g_set.muteMinimized; }
                DWORD pid = GamePid();
                HWND gw = FindGameWindow(pid);
                int want = (opt && gw && IsIconic(gw)) ? 1 : 0;
                if (want != muted && (want == 1 || muted == 1 || muted == -1)) {
                    if (SetProcessMuted(pid, want == 1)) { if (muted != want) LogF("game sound %s", want ? "off" : "on"); muted = want; }
                    else if (muted == -1 && want == 0) muted = 0;
                }
            }
        }
        Sleep(100);
    }
}

void GameInit()
{
    if (GetFileAttributesW(g_gameExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        LogF("Loadout.exe not found: %s", W2U(g_gameExe).c_str());
        g_gameState = GS_NOEXE;
    } else {
        GameApplyPatch();      // game just downloaded / folder just chosen: patch on by default
        int mode; { std::lock_guard<std::mutex> lk(g_setMu); mode = g_set.launchMode; }
        if (mode == 1 && !FindGamePid()) g_launchReq = true;   // mode 0: the player chooses first and presses "Launch game"
    }
    g_worker = std::thread(WorkerMain);
}

void GameRequestLaunch() { if (g_gameState != GS_NOEXE) g_launchReq = true; }

void GameShutdown()
{
    g_quit = true;
    if (g_worker.joinable()) g_worker.join();
    { DWORD pid = GamePid(); if (pid) { CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); SetProcessMuted(pid, false); } }
    CloseGameHandle();
    // the launcher is closing: close the game too (politely first, then for sure)
    if (DWORD pid = FindGamePid()) {
        HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (HWND w = FindGameWindow(pid)) PostMessageW(w, WM_CLOSE, 0, 0);
        if (h) {
            if (WaitForSingleObject(h, 3000) != WAIT_OBJECT_0) { TerminateProcess(h, 0); WaitForSingleObject(h, 3000); }
            CloseHandle(h);
        }
        LogF("game closed together with the launcher");
    }
    if (!FindGamePid()) GameEmuStopped();   // game not running: give the Steam registry values back
}

void BringSelfToFront()
{
    if (IsIconic(g_mainHwnd)) ShowWindow(g_mainHwnd, SW_RESTORE);
    ForceForeground(g_mainHwnd);
}
