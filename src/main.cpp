// Loadout Launcher — one program instead of SmartSteamEmu launcher + console patcher.
#include <windows.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <shellapi.h>
#include <shlobj.h>
#include <d3d11.h>
#include <string>
#include <vector>
#include <map>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <cmath>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "game.h"
#include "data.h"
#include "ui.h"
#include "setup.h"

#ifdef LL_TESTHOOK
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#endif

// ------------------------------------------------------------------ D3D
static ID3D11Device* g_dev = nullptr;
static ID3D11DeviceContext* g_ctx = nullptr;
static IDXGISwapChain* g_swap = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static UINT g_resizeW = 0, g_resizeH = 0;
static bool g_occluded = false;
static float g_newS = 0.0f;     // pending DPI change
static float g_dpiS = 1.0f;     // scale of the monitor; S can be smaller when the window is small

static void CreateRT()
{
    ID3D11Texture2D* bb = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&bb));
    g_dev->CreateRenderTargetView(bb, nullptr, &g_rtv);
    bb->Release();
}
static void CleanupRT() { if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; } }

static bool CreateDevice(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL fls[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT r = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, fls, 2, D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &fl, &g_ctx);
    if (r == DXGI_ERROR_UNSUPPORTED)
        r = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, fls, 2, D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &fl, &g_ctx);
    if (r != S_OK) return false;
    CreateRT();
    return true;
}
static void CleanupDevice()
{
    CleanupRT();
    if (g_swap) g_swap->Release();
    if (g_ctx) g_ctx->Release();
    if (g_dev) g_dev->Release();
}

#ifdef LL_TESTHOOK
// test hook: commands from LoadoutLauncher.test (click x y / shot) — debug builds only
static std::vector<std::string> g_testCmds;
static bool g_wantShot = false;
static std::string g_shotName = "shot";
static void TestPoll()
{
    static double last = 0;
    if (ImGui::GetTime() - last < 0.5) return;
    last = ImGui::GetTime();
    std::wstring p = AppDataDir() + L"\\LoadoutLauncher.test";
    FILE* f = _wfopen(p.c_str(), L"r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) g_testCmds.push_back(line);
    fclose(f);
    DeleteFileW(p.c_str());
}

static void ScreenShotDesktop()
{
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN), h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    HDC sdc = GetDC(nullptr), mdc = CreateCompatibleDC(sdc);
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mdc, bmp);
    BitBlt(mdc, 0, 0, w, h, sdc, x, y, SRCCOPY | CAPTUREBLT);
    int ow = w / 2, oh = h / 2;
    std::vector<unsigned char> px(ow * oh * 3);
    for (int j = 0; j < oh; j++) for (int i = 0; i < ow; i++) { unsigned char* p = (unsigned char*)bits + ((j * 2) * w + i * 2) * 4; unsigned char* o = &px[(j * ow + i) * 3]; o[0] = p[2]; o[1] = p[1]; o[2] = p[0]; }
    SelectObject(mdc, old); DeleteObject(bmp); DeleteDC(mdc); ReleaseDC(nullptr, sdc);
    std::wstring f = AppDataDir() + L"\\LoadoutLauncher_screen.png";
    char pa[MAX_PATH]; WideCharToMultiByte(CP_ACP, 0, f.c_str(), -1, pa, MAX_PATH, nullptr, nullptr);
    stbi_write_png(pa, ow, oh, 3, px.data(), ow * 3);
}
static int g_testWait = 0;
static ULONGLONG g_testSleepUntil = 0;
static float g_clickX, g_clickY; static int g_clickPhase = 0;
static bool g_holdMouse = false; static float g_holdX, g_holdY;
static void TestStep()
{
    ImGuiIO& io = ImGui::GetIO();
    if (g_holdMouse && !g_clickPhase) io.AddMousePosEvent(g_holdX, g_holdY);
    if (g_clickPhase) {
        io.AddMousePosEvent(g_clickX, g_clickY);
        if (g_clickPhase == 3) io.AddMouseButtonEvent(0, true);
        if (g_clickPhase == 5) { io.AddMouseButtonEvent(0, false); g_clickPhase = 0; } else g_clickPhase++;
    }
    if (g_testWait > 0) { g_testWait--; return; }
    if (GetTickCount64() < g_testSleepUntil) return;
    if (g_testCmds.empty()) return;
    std::string c = g_testCmds.front(); g_testCmds.erase(g_testCmds.begin());
    float x, y; int n;
    if (sscanf(c.c_str(), "click %f %f", &x, &y) == 2) {
        g_clickX = x; g_clickY = y; g_clickPhase = 1; g_testWait = 8;
    } else if (sscanf(c.c_str(), "move %f %f", &x, &y) == 2) { io.AddMousePosEvent(x, y); g_holdMouse = true; g_holdX = x; g_holdY = y; g_testWait = 5; }
    else if (sscanf(c.c_str(), "sleep %d", &n) == 1) g_testSleepUntil = GetTickCount64() + n;
    else if (sscanf(c.c_str(), "lang %d", &n) == 1) { { std::lock_guard<std::mutex> lk(g_setMu); g_set.lang = n; } SettingsSave(); g_testWait = 10; }
    else if (sscanf(c.c_str(), "size %f %f", &x, &y) == 2) { SetWindowPos(g_mainHwnd, nullptr, 0, 0, (int)x, (int)y, SWP_NOMOVE | SWP_NOZORDER); g_testWait = 10; }
    else if (sscanf(c.c_str(), "unc %d", &n) == 1) { { std::lock_guard<std::mutex> lk(g_setMu); g_set.uncensored = n != 0; } SettingsSave(); LogF("test unc %d -> %d", n, GameApplyPatch()); }
    else if (sscanf(c.c_str(), "scale %f", &x) == 1) { g_newS = x; g_testWait = 10; }
    else if (sscanf(c.c_str(), "wheel %d", &n) == 1) { io.AddMouseWheelEvent(0, (float)n); g_testWait = 5; }
    else if (c.rfind("shot", 0) == 0) {
        g_shotName = c.size() > 5 ? c.substr(5) : "shot";
        while (!g_shotName.empty() && (g_shotName.back() == '\r' || g_shotName.back() == '\n' || g_shotName.back() == ' ')) g_shotName.pop_back();
        if (g_shotName.empty()) g_shotName = "shot";
        g_wantShot = true; g_testWait = 3;
    }
    else if (sscanf(c.c_str(), "wait %d", &n) == 1) g_testWait = n;
    else if (c.rfind("quit", 0) == 0) PostQuitMessage(0);
    else if (c.rfind("screen", 0) == 0) ScreenShotDesktop();
    else if (c.rfind("return", 0) == 0) GameReturnToGame();
    else if (c.rfind("killgame", 0) == 0) { DWORD pid = GamePid(); HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid); if (h) { TerminateProcess(h, 1); CloseHandle(h); } }
    else if (c.rfind("launch", 0) == 0) GameRequestLaunch();
    else if (c.rfind("setupdl ", 0) == 0) { std::string a = c.substr(8); while (!a.empty() && (a.back() == '\n' || a.back() == '\r')) a.pop_back(); SetupStartDownload(U2W(a)); }
    else if (c.rfind("setupcancel", 0) == 0) SetupCancel();
    else if (c.rfind("move ", 0) == 0) { std::string a = c.substr(5); while (!a.empty() && (a.back() == '\n' || a.back() == '\r')) a.pop_back(); if (SetupMoveLauncher(U2W(a), false)) PostQuitMessage(0); }
}
static void TestShot()
{
    if (!g_wantShot) return;
    g_wantShot = false;
    ID3D11Texture2D* bb = nullptr; g_swap->GetBuffer(0, IID_PPV_ARGS(&bb));
    D3D11_TEXTURE2D_DESC d; bb->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
    ID3D11Texture2D* st = nullptr; g_dev->CreateTexture2D(&d, nullptr, &st);
    g_ctx->CopyResource(st, bb);
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(g_ctx->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
        std::vector<unsigned char> px(d.Width * d.Height * 4);
        for (UINT y = 0; y < d.Height; y++) memcpy(&px[y * d.Width * 4], (char*)m.pData + y * m.RowPitch, d.Width * 4);
        for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
        g_ctx->Unmap(st, 0);
        std::wstring p = AppDataDir() + L"\\LoadoutLauncher_" + U2W(g_shotName) + L".png";
        char pa[MAX_PATH]; WideCharToMultiByte(CP_ACP, 0, p.c_str(), -1, pa, MAX_PATH, nullptr, nullptr);
        stbi_write_png(pa, d.Width, d.Height, 4, px.data(), d.Width * 4);
    }
    st->Release(); bb->Release();
}
#endif

// ------------------------------------------------------------------ window
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_uiCaptureHotkey && (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN)) {
        int vk = (int)wParam;
        if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN) return 0;
        int mods = ((GetKeyState(VK_CONTROL) & 0x8000) ? 1 : 0) | ((GetKeyState(VK_MENU) & 0x8000) ? 2 : 0) | ((GetKeyState(VK_SHIFT) & 0x8000) ? 4 : 0);
        UiHotkeyCaptured(mods, vk);
        return 0;
    }
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_ACTIVATEAPP:
        if (wParam) SettingsLoad();   // the in-game overlay may have changed favorites
        break;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_resizeW = LOWORD(lParam); g_resizeH = HIWORD(lParam);
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lParam;
        mm->ptMinTrackSize.x = (LONG)(760 * g_dpiS); mm->ptMinTrackSize.y = (LONG)(500 * g_dpiS);
        return 0;
    }
    case WM_DPICHANGED: {
        g_newS = HIWORD(wParam) / 96.0f;
        RECT* r = (RECT*)lParam;
        SetWindowPos(hWnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_APP + 1:
        BringSelfToFront();
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int)
{
    // single instance
    bool afterMove = wcsstr(GetCommandLineW(), L"--cleanup") != nullptr;
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"LoadoutReloadedLauncher_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS && afterMove) {
        // the old copy is still closing: wait for it
        DWORD r = WaitForSingleObject(mtx, 15000);
        if (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED) SetLastError(0);
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(L"LoadoutLauncherWnd", nullptr);
        if (other) { ShowWindow(other, SW_RESTORE); SetForegroundWindow(other); }
        return 0;
    }

    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    g_exeDir = exe; g_exeDir = g_exeDir.substr(0, g_exeDir.rfind(L'\\'));
    g_gameExe = g_exeDir + L"\\Loadout.exe";
    if (GetFileAttributesW(g_gameExe.c_str()) != INVALID_FILE_ATTRIBUTES) {
        g_dataRoot = g_exeDir;
        // earlier versions kept these files in %LOCALAPPDATA%\LoadoutLauncher — clean that up
        wchar_t* la = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &la))) {
            std::wstring old = std::wstring(la) + L"\\LoadoutLauncher";
            CoTaskMemFree(la);
            if (GetFileAttributesW(old.c_str()) != INVALID_FILE_ATTRIBUTES) {
                std::wstring from = old; from.push_back(0);
                SHFILEOPSTRUCTW op{}; op.wFunc = FO_DELETE; op.pFrom = from.c_str(); op.fFlags = FOF_NO_UI;
                SHFileOperationW(&op);
            }
        }
    }
    if (!g_dataRoot.empty())   // files left in the game folder by older versions
        for (const wchar_t* f : {L"LoadoutLauncher.log", L"LoadoutLauncher.test", L"LoadoutLauncher_shot.png"}) DeleteFileW((g_exeDir + L"\\" + f).c_str());
#ifndef LL_TESTHOOK
    for (const wchar_t* f : {L"LoadoutLauncher.test", L"LoadoutLauncher_shot.png", L"LoadoutLauncher_screen.png", L"LoadoutOverlay.test", L"LoadoutOverlay_shot.png"})
        if (!g_dataRoot.empty()) DeleteFileW((AppDataDir() + L"\\" + f).c_str());   // test leftovers
#endif
    LogInit();
    LogF("Loadout Launcher start, folder: %ls", g_exeDir.c_str());
    g_iniPath = g_exeDir + L"\\LoadoutLauncher.ini";
    if (!g_dataRoot.empty()) {   // settings live in LoadoutLauncher_Data; move the old file there
        std::wstring oldIni = g_iniPath;
        g_iniPath = AppDataDir() + L"\\LoadoutLauncher.ini";
        if (GetFileAttributesW(oldIni.c_str()) != INVALID_FILE_ATTRIBUTES) {
            if (!MoveFileExW(oldIni.c_str(), g_iniPath.c_str(), MOVEFILE_COPY_ALLOWED)) DeleteFileW(oldIni.c_str());
        }
    }
    SettingsLoad();
    if (GetFileAttributesW(g_gameExe.c_str()) != INVALID_FILE_ATTRIBUTES) SettingsSave();   // no ini outside the game folder
    {
        int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; argv && i + 1 < argc; i++) if (!wcscmp(argv[i], L"--cleanup")) SetupCleanupOld(argv[i + 1]);
        if (argv) LocalFree(argv);
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ImGui_ImplWin32_EnableDpiAwareness();
    POINT pt; GetCursorPos(&pt);
    S = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY));
    g_dpiS = S;

    WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, WndProc, 0, 0, hInst, LoadIconW(hInst, MAKEINTRESOURCEW(1)), LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr, L"LoadoutLauncherWnd", LoadIconW(hInst, MAKEINTRESOURCEW(1))};
    RegisterClassExW(&wc);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{}; mi.cbSize = sizeof(mi); GetMonitorInfoW(mon, &mi);
    int mw = mi.rcWork.right - mi.rcWork.left, mh = mi.rcWork.bottom - mi.rcWork.top;
    int ww = (int)(1360 * S), wh = (int)(860 * S);
    if (ww > mw * 0.92) ww = (int)(mw * 0.92);
    if (wh > mh * 0.92) wh = (int)(mh * 0.92);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Loadout Launcher", WS_OVERLAPPEDWINDOW,
                                mi.rcWork.left + (mw - ww) / 2, mi.rcWork.top + (mh - wh) / 2, ww, wh, nullptr, nullptr, hInst, nullptr);
    g_mainHwnd = hwnd;
    {   // no program icon and no title text in the title bar (the taskbar keeps both)
        WTA_OPTIONS o{WTNCA_NODRAWICON | WTNCA_NODRAWCAPTION | WTNCA_NOSYSMENU, WTNCA_NODRAWICON | WTNCA_NODRAWCAPTION};
        SetWindowThemeAttribute(hwnd, WTA_NONCLIENT, &o, sizeof(o));
    }
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    COLORREF cap = RGB(24, 26, 30);
    DwmSetWindowAttribute(hwnd, 35, &cap, sizeof(cap));

    if (!CreateDevice(hwnd)) { CleanupDevice(); MessageBoxW(nullptr, L"Direct3D 11 init failed", L"Loadout Launcher", MB_ICONERROR); return 1; }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    g_host.overlay = false;
    g_host.writeMap = [](const std::string& c) { return GameWriteMapNow(c); };
    g_host.currentMap = []() { return GameLastWrittenMap(); };
    g_host.gameState = []() { return (int)g_gameState; };
    g_host.returnToGame = []() { GameReturnToGame(); };
    g_host.launchGame = []() { GameRequestLaunch(); };
    g_host.gameFolder = g_exeDir;
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);
    UiInit(g_dev, g_ctx, nullptr, S);

    // first frame before showing, to avoid a white flash
    const float clear[4] = {17 / 255.f, 18 / 255.f, 21 / 255.f, 1};
    g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_ctx->ClearRenderTargetView(g_rtv, clear);
    g_swap->Present(0, 0);
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    GameInit();

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;
#ifdef LL_TESTHOOK
        if (IsIconic(hwnd) || g_occluded) {
            std::wstring tp = AppDataDir() + L"\\LoadoutLauncher.test";
            FILE* tf = _wfopen(tp.c_str(), L"r");
            if (tf) {
                char l[64] = ""; fgets(l, 64, tf); fclose(tf);
                if (!strncmp(l, "quit", 4)) { DeleteFileW(tp.c_str()); break; }
                if (!strncmp(l, "screen", 6)) { DeleteFileW(tp.c_str()); ScreenShotDesktop(); }
            }
        }
#endif
        if (IsIconic(hwnd) || (g_occluded && g_swap->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)) { Sleep(50); continue; }
        g_occluded = false;
        if (g_resizeW && g_resizeH) {
            CleanupRT();
            g_swap->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeW = g_resizeH = 0;
            CreateRT();
        }
        if (g_newS > 0) { g_dpiS = g_newS; g_newS = 0; }
        {   // fit the layout to the window: on small screens everything shrinks evenly instead of being cut off
            RECT rc; GetClientRect(hwnd, &rc);
            float fit = std::min(g_dpiS, std::min((rc.right - rc.left) / 1240.0f, (rc.bottom - rc.top) / 780.0f));
            if (fit < g_dpiS * 0.55f) fit = g_dpiS * 0.55f;
            fit = std::floor(fit * 40.0f) / 40.0f;
            if (fit > 0.3f && std::fabs(fit - S) > 0.001f) UiSetScale(fit);
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
#ifdef LL_TESTHOOK
        TestPoll(); TestStep();
#endif
        ImGui::NewFrame();
        UiDraw();
        ImGui::Render();
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
#ifdef LL_TESTHOOK
        TestShot();
#endif
        HRESULT hr = g_swap->Present(1, 0);
        g_occluded = hr == DXGI_STATUS_OCCLUDED;
    }

    if (IsWindow(hwnd)) ShowWindow(hwnd, SW_HIDE);
    GameShutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDevice();
    DestroyWindow(hwnd);
    if (mtx) CloseHandle(mtx);
    LogF("exit");
    return 0;
}
