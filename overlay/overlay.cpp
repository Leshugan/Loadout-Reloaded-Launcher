// LoadoutOverlay.dll — the launcher UI inside the game (32-bit, loaded into Loadout.exe as a SmartSteamEmu plugin).
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <string>
#include <atomic>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <tlhelp32.h>
#include "MinHook.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "../src/ui.h"
#include "../src/settings.h"
#ifdef LL_TESTHOOK
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <vector>
#endif

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static HMODULE g_self = nullptr;
static std::wstring g_gameDir;
static FILE* g_log = nullptr;

static void Log(const char* fmt, ...)
{
    if (!g_log) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
}

// ------------------------------------------------------------------ game memory (we are inside Loadout.exe)
static const uintptr_t ADDR_MAP = 0x0cc94d0;

static bool WriteMap(const std::string& code)
{
    if (code.empty() || code.size() > 60) return false;
    SIZE_T wr = 0;
    BOOL ok = WriteProcessMemory(GetCurrentProcess(), (LPVOID)ADDR_MAP, code.c_str(), code.size() + 1, &wr);
    if (!ok) {
        DWORD old;
        if (VirtualProtect((LPVOID)ADDR_MAP, code.size() + 1, PAGE_READWRITE, &old)) {
            memcpy((void*)ADDR_MAP, code.c_str(), code.size() + 1);
            VirtualProtect((LPVOID)ADDR_MAP, code.size() + 1, old, &old);
            ok = TRUE;
        }
    }
    Log("map -> %s : %s", code.c_str(), ok ? "ok" : "FAILED");
    return ok != 0;
}

static std::string ReadMap()
{
    char buf[72] = {};
    SIZE_T rd = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)ADDR_MAP, buf, 64, &rd)) return {};
    std::string s;
    for (int i = 0; i < 64 && buf[i]; i++) { unsigned char c = (unsigned char)buf[i]; if (c < 32 || c > 126) break; s += (char)c; }
    return s;
}

// ------------------------------------------------------------------ state
static std::atomic<bool> g_enabled{false};
static std::atomic<int> g_mods{4}, g_vk{VK_TAB};
static bool g_hooked = false;
static bool g_ready = false;
static bool g_open = false;
static bool g_keyWasDown = false;
static HWND g_gameHwnd = nullptr;
static WNDPROC g_origWndProc = nullptr;
static ID3D11Device* g_dev = nullptr;
static ID3D11DeviceContext* g_ctx = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static IDXGISwapChain* g_sc = nullptr;
static POINT g_frozenCursor{};
static thread_local int g_ourCall = 0;

typedef HRESULT(STDMETHODCALLTYPE* PresentFn)(IDXGISwapChain*, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* ResizeFn)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
static PresentFn oPresent = nullptr;
static ResizeFn oResize = nullptr;

typedef BOOL(WINAPI* GetCursorPosFn)(LPPOINT);
typedef BOOL(WINAPI* SetCursorPosFn)(int, int);
typedef BOOL(WINAPI* ClipCursorFn)(const RECT*);
typedef UINT(WINAPI* GetRawInputBufferFn)(PRAWINPUT, PUINT, UINT);
static GetCursorPosFn oGetCursorPos = nullptr;
static SetCursorPosFn oSetCursorPos = nullptr;
static ClipCursorFn oClipCursor = nullptr;
static GetRawInputBufferFn oGetRawInputBuffer = nullptr;

static void SetOpen(bool open)
{
    if (open == g_open) return;
    g_open = open;
    Log("overlay %s", open ? "opened" : "closed");
    if (open) {
        SettingsLoad();
        UiOnOpen();
        if (oGetCursorPos) oGetCursorPos(&g_frozenCursor); else GetCursorPos(&g_frozenCursor);
        if (oClipCursor) oClipCursor(nullptr);
        ImGui::GetIO().ClearInputKeys();
    }
}

// ------------------------------------------------------------------ input hooks (only act while the overlay is open)
static BOOL WINAPI hkGetCursorPos(LPPOINT p)
{
    if (g_open && !g_ourCall && p) { *p = g_frozenCursor; return TRUE; }
    return oGetCursorPos(p);
}
static BOOL WINAPI hkSetCursorPos(int x, int y)
{
    if (g_open && !g_ourCall) return TRUE;
    return oSetCursorPos(x, y);
}
static BOOL WINAPI hkClipCursor(const RECT* r)
{
    if (g_open && !g_ourCall) return oClipCursor(nullptr);
    return oClipCursor(r);
}
static UINT WINAPI hkGetRawInputBuffer(PRAWINPUT data, PUINT size, UINT hdr)
{
    UINT r = oGetRawInputBuffer(data, size, hdr);
    if (g_open && data && r != (UINT)-1) return 0;   // drained, nothing for the game
    return r;
}

static LRESULT CALLBACK hkWndProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    if (g_open && g_ready) {
        g_ourCall++;
        ImGui_ImplWin32_WndProcHandler(h, msg, w, l);
        g_ourCall--;
        switch (msg) {
        case WM_INPUT:
            return DefWindowProcW(h, msg, w, l);
        case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK: case WM_MBUTTONDOWN: case WM_MBUTTONUP:
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: case WM_XBUTTONDOWN: case WM_XBUTTONUP:
        case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR: case WM_SYSCHAR: case WM_SYSKEYUP:
            return 0;
        case WM_SYSKEYDOWN:
            if (w == VK_F4) break;   // keep Alt+F4 working
            return 0;
        case WM_SETCURSOR:
            SetCursor(nullptr);
            return TRUE;
        }
    }
    return CallWindowProcW(g_origWndProc, h, msg, w, l);
}

// ------------------------------------------------------------------ rendering
static void Teardown()
{
    if (!g_ready) return;
    g_open = false;
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    if (g_origWndProc && g_gameHwnd && IsWindow(g_gameHwnd)) SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)g_origWndProc);
    g_origWndProc = nullptr;
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    if (g_ctx) { g_ctx->Release(); g_ctx = nullptr; }
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }
    g_ready = false;
    Log("ui released");
}

static bool InitImGui(IDXGISwapChain* sc)
{
    DXGI_SWAP_CHAIN_DESC d0{};
    if (FAILED(sc->GetDesc(&d0)) || d0.BufferDesc.Width < 320 || d0.BufferDesc.Height < 240) return false;   // not the real game window yet
    if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&g_dev))) { static bool once = false; if (!once) { once = true; Log("GetDevice failed (not a D3D11 game?)"); } return false; }
    g_dev->GetImmediateContext(&g_ctx);
    DXGI_SWAP_CHAIN_DESC d{};
    sc->GetDesc(&d);
    g_gameHwnd = d.OutputWindow;
    float scale = d.BufferDesc.Height / 1080.0f;
    if (scale < 0.75f) scale = 0.75f;
    if (scale > 2.0f) scale = 2.0f;
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui_ImplWin32_Init(g_gameHwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);
    g_host.overlay = true;
    g_host.writeMap = [](const std::string& c) { return WriteMap(c); };
    g_host.currentMap = []() { return ReadMap(); };
    g_host.gameState = []() { return (int)GS_READY; };
    g_host.returnToGame = []() { SetOpen(false); };
    g_host.launchGame = []() {};
    g_host.gameFolder = g_gameDir;
    UiInit(g_dev, g_ctx, g_self, scale);
    g_origWndProc = (WNDPROC)SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
    Log("ui ready: window %p, %ux%u, scale %.2f", g_gameHwnd, d.BufferDesc.Width, d.BufferDesc.Height, scale);
    return true;
}

static void CheckHotkey()
{
    if (GetForegroundWindow() != g_gameHwnd) { g_keyWasDown = false; return; }
    int m = g_mods, vk = g_vk;
    bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    bool down = (GetAsyncKeyState(vk) & 0x8000) && ctrl == ((m & 1) != 0) && alt == ((m & 2) != 0) && shift == ((m & 4) != 0);
    if (down && !g_keyWasDown) SetOpen(!g_open);
    g_keyWasDown = down;
}


#ifdef LL_TESTHOOK
// test hook: %LOCALAPPDATA%\LoadoutLauncher\LoadoutOverlay.test with "open", "close" or "shot"
static std::atomic<int> g_testCmd{0};   // 1 open, 2 close, 3 shot
static void TestShot(IDXGISwapChain* sc)
{
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))) return;
    D3D11_TEXTURE2D_DESC d; bb->GetDesc(&d);
    Log("backbuffer format %d, %ux%u, msaa %u", d.Format, d.Width, d.Height, d.SampleDesc.Count);
    D3D11_TEXTURE2D_DESC sd = d; sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MiscFlags = 0; sd.SampleDesc.Count = 1; sd.SampleDesc.Quality = 0;
    ID3D11Texture2D* st = nullptr;
    if (SUCCEEDED(g_dev->CreateTexture2D(&sd, nullptr, &st))) {
        if (d.SampleDesc.Count > 1) g_ctx->ResolveSubresource(st, 0, bb, 0, d.Format); else g_ctx->CopyResource(st, bb);
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(g_ctx->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
            int w = d.Width / 2, h = d.Height / 2;
            std::vector<unsigned char> px(w * h * 3);
            bool bgra = d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
                unsigned char* p = (unsigned char*)m.pData + (y * 2) * m.RowPitch + (x * 2) * 4;
                unsigned char* o = &px[(y * w + x) * 3];
                o[0] = bgra ? p[2] : p[0]; o[1] = p[1]; o[2] = bgra ? p[0] : p[2];
            }
            g_ctx->Unmap(st, 0);
            std::wstring f = AppDataDir() + L"\\LoadoutOverlay_shot.png";
            char pa[MAX_PATH]; WideCharToMultiByte(CP_ACP, 0, f.c_str(), -1, pa, MAX_PATH, nullptr, nullptr);
            stbi_write_png(pa, w, h, 3, px.data(), w * 3);
            Log("shot saved");
        }
        st->Release();
    }
    bb->Release();
}
static void TestPoll()
{
    std::wstring p = AppDataDir() + L"\\LoadoutOverlay.test";
    FILE* f = _wfopen(p.c_str(), L"r");
    if (!f) return;
    char l[32] = ""; fgets(l, 32, f); fclose(f); DeleteFileW(p.c_str());
    if (!strncmp(l, "open", 4)) g_testCmd = 1; else if (!strncmp(l, "close", 5)) g_testCmd = 2; else if (!strncmp(l, "shot", 4)) g_testCmd = 3;
}
#endif

static HRESULT STDMETHODCALLTYPE hkPresent(IDXGISwapChain* sc, UINT sync, UINT flags)
{
    if (g_enabled || g_open) {
        if (g_ready && sc != g_sc) Teardown();          // the game made a new swap chain / window
        if (!g_ready) { g_ready = InitImGui(sc); if (g_ready) g_sc = sc; }
        if (g_ready) {
            if (g_enabled) CheckHotkey(); else SetOpen(false);
#ifdef LL_TESTHOOK
            { int c = g_testCmd.exchange(0); if (c == 1) SetOpen(true); else if (c == 2) SetOpen(false); else if (c == 3) { if (g_open) { /* shot after drawing */ g_testCmd = 4; } else TestShot(sc); } }
#endif
            RECT cr{}; GetClientRect(g_gameHwnd, &cr);
            bool visible = !IsIconic(g_gameHwnd) && cr.right - cr.left > 64 && cr.bottom - cr.top > 64;
            if (g_open && visible) {
                if (!g_rtv) {
                    ID3D11Texture2D* bb = nullptr;
                    if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))) { g_dev->CreateRenderTargetView(bb, nullptr, &g_rtv); bb->Release(); }
                }
                if (g_rtv) {
                    g_ourCall++;
                    ImGui_ImplDX11_NewFrame();
                    ImGui_ImplWin32_NewFrame();
                    ImGui::NewFrame();
                    ImGui::GetIO().MouseDrawCursor = true;
                    UiDraw();
                    ImGui::Render();
                    ID3D11RenderTargetView* oldRtv = nullptr; ID3D11DepthStencilView* oldDsv = nullptr;
                    g_ctx->OMGetRenderTargets(1, &oldRtv, &oldDsv);
                    g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
                    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                    g_ctx->OMSetRenderTargets(1, &oldRtv, oldDsv);
                    if (oldRtv) oldRtv->Release();
                    if (oldDsv) oldDsv->Release();
                    g_ourCall--;
                    if (oClipCursor) oClipCursor(nullptr);
#ifdef LL_TESTHOOK
                    if (g_testCmd == 4) { g_testCmd = 0; TestShot(sc); }
#endif
                }
            }
        }
    }
    return oPresent(sc, sync, flags);
}

static HRESULT STDMETHODCALLTYPE hkResize(IDXGISwapChain* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT fl)
{
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    return oResize(sc, n, w, h, f, fl);
}

// ------------------------------------------------------------------ hook setup
static bool InstallHooks()
{
    WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, DefWindowProcW, 0, 0, GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr, L"LoadoutOverlayDummy", nullptr};
    RegisterClassExW(&wc);
    HWND hw = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hw;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* sc = nullptr; ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev, &fl, &ctx);
    if (FAILED(hr)) hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev, &fl, &ctx);
    if (FAILED(hr)) { Log("dummy swap chain failed 0x%08lx", hr); DestroyWindow(hw); return false; }
    void** vt = *(void***)sc;
    void* present = vt[8];
    void* resize = vt[13];
    sc->Release(); ctx->Release(); dev->Release();
    DestroyWindow(hw);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    if (MH_Initialize() != MH_OK) { Log("MH_Initialize failed"); return false; }
    bool ok = MH_CreateHook(present, (void*)hkPresent, (void**)&oPresent) == MH_OK &&
              MH_CreateHook(resize, (void*)hkResize, (void**)&oResize) == MH_OK;
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    MH_CreateHook((void*)GetProcAddress(u32, "GetCursorPos"), (void*)hkGetCursorPos, (void**)&oGetCursorPos);
    MH_CreateHook((void*)GetProcAddress(u32, "SetCursorPos"), (void*)hkSetCursorPos, (void**)&oSetCursorPos);
    MH_CreateHook((void*)GetProcAddress(u32, "ClipCursor"), (void*)hkClipCursor, (void**)&oClipCursor);
    MH_CreateHook((void*)GetProcAddress(u32, "GetRawInputBuffer"), (void*)hkGetRawInputBuffer, (void**)&oGetRawInputBuffer);
    if (!ok || MH_EnableHook(MH_ALL_HOOKS) != MH_OK) { Log("hooking failed"); return false; }
    Log("hooks installed (dinput8 %s)", GetModuleHandleW(L"dinput8.dll") ? "loaded" : "not loaded");
    return true;
}

static DWORD WINAPI MainThread(LPVOID)
{
    Sleep(2000);
#ifdef LL_TESTHOOK
    Sleep(10000);
    { HANDLE sn = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
      MODULEENTRY32W me{}; me.dwSize = sizeof(me);
      for (BOOL ok = Module32FirstW(sn, &me); ok; ok = Module32NextW(sn, &me)) Log("MOD %ls", me.szExePath);
      CloseHandle(sn); }
#endif
    while (true) {
        SettingsLoad();
        { std::lock_guard<std::mutex> lk(g_setMu); g_enabled = g_set.overlayEnabled; g_mods = g_set.overlayMods; g_vk = g_set.overlayVk; }
        if (g_enabled && !g_hooked) { g_hooked = true; if (!InstallHooks()) Log("overlay unavailable"); }
#ifdef LL_TESTHOOK
        TestPoll();
#endif
        Sleep(1000);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = h;
        DisableThreadLibraryCalls(h);
        wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring e = exe;
        size_t sl = e.rfind(L'\\');
        std::wstring name = e.substr(sl + 1);
        if (_wcsicmp(name.c_str(), L"Loadout.exe") != 0) return TRUE;   // only inside the game
        g_gameDir = e.substr(0, sl);
        g_dataRoot = g_gameDir;
        g_iniPath = g_gameDir + L"\\LoadoutLauncher_Data\\LoadoutLauncher.ini";
        g_log = _wfopen((AppDataDir() + L"\\LoadoutOverlay.log").c_str(), L"w");
        Log("overlay loaded into %ls", exe);
#ifdef LL_TESTHOOK
        if (wchar_t* env = GetEnvironmentStringsW()) {
            for (wchar_t* p = env; *p; p += wcslen(p) + 1) Log("ENV %ls", p);
            FreeEnvironmentStringsW(env);
        }
        Log("CMD %ls", GetCommandLineW());
        HKEY k;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", 0, KEY_READ, &k) == 0) {
            DWORD pid = 0, sz = 4; RegQueryValueExW(k, L"pid", nullptr, nullptr, (BYTE*)&pid, &sz);
            wchar_t b[600] = {}; sz = sizeof(b) - 2; RegQueryValueExW(k, L"SteamClientDll", nullptr, nullptr, (BYTE*)b, &sz);
            wchar_t b2[600] = {}; sz = sizeof(b2) - 2; RegQueryValueExW(k, L"SteamClientDll64", nullptr, nullptr, (BYTE*)b2, &sz);
            Log("REG pid=%lu dll=%ls dll64=%ls", pid, b, b2); RegCloseKey(k);
        }
        DWORD ppid = 0; HANDLE sn = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
        for (BOOL ok = Process32FirstW(sn, &pe); ok; ok = Process32NextW(sn, &pe)) if (pe.th32ProcessID == GetCurrentProcessId()) ppid = pe.th32ParentProcessID;
        CloseHandle(sn); Log("PARENT %lu", ppid);
#endif
        CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
