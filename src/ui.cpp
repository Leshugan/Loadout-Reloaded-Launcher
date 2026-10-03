// Shared launcher UI (launcher window + in-game overlay).
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <thread>
#include <atomic>
#include <algorithm>
#include <d3d11.h>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <cstring>
#include <cmath>
#include <cfloat>
#include "imgui.h"
#include "ui.h"
#include "settings.h"
#include "data.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "stb_image.h"

UiHost g_host;
float S = 1.0f;
bool g_uiCaptureHotkey = false;
static ID3D11Device* s_dev = nullptr;
static ID3D11DeviceContext* s_ctx = nullptr;
static HMODULE s_mod = nullptr;

// ------------------------------------------------------------------ textures
struct Tex { ImTextureID id = 0; int w = 0, h = 0; };
static std::map<std::string, Tex> g_tex;

static Tex TexFromMemory(const void* data, int size)
{
    Tex t;
    int w, h, n;
    unsigned char* px = stbi_load_from_memory((const unsigned char*)data, size, &w, &h, &n, 4);
    if (!px) return t;
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w; d.Height = h; d.MipLevels = 0; d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    d.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    ID3D11Texture2D* tx = nullptr;
    if (SUCCEEDED(s_dev->CreateTexture2D(&d, nullptr, &tx))) {
        s_ctx->UpdateSubresource(tx, 0, nullptr, px, w * 4, 0);
        ID3D11ShaderResourceView* srv = nullptr;
        D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = (UINT)-1;
        s_dev->CreateShaderResourceView(tx, &sv, &srv);
        if (srv) s_ctx->GenerateMips(srv);
        tx->Release();
        t.id = (ImTextureID)(intptr_t)srv; t.w = w; t.h = h;
    }
    stbi_image_free(px);
    return t;
}

static void LoadTextures()
{
    const char* names[] = {"brewery","drillcavern","drillcavern_night","fissure","fissure_night","fourpoints","shattered",
                           "trailerpark","trailerpark_night","trailerpark_mu","shootinggallery","spires","tower","shippingyard","twoports","icon"};
    for (const char* n : names) {
        std::string rn = std::string("IMG_") + n;
        HRSRC r = FindResourceA(s_mod, rn.c_str(), (LPCSTR)RT_RCDATA);
        if (!r) { continue; }
        HGLOBAL g = LoadResource(s_mod, r);
        g_tex[n] = TexFromMemory(LockResource(g), (int)SizeofResource(s_mod, r));
    }
}
static ImTextureID TexId(const char* n)
{
    if (!n) return 0;
    auto it = g_tex.find(n);
    return it == g_tex.end() ? 0 : it->second.id;
}

// ------------------------------------------------------------------ theme
static ImFont* fReg = nullptr;
static ImFont* fSemi = nullptr;
static ImFont* fBold = nullptr;

static const ImU32 C_BG = IM_COL32(17, 18, 21, 255);
static const ImU32 C_PANEL = IM_COL32(24, 26, 30, 255);
static const ImU32 C_CARD = IM_COL32(32, 35, 41, 255);
static const ImU32 C_CARD_HOV = IM_COL32(40, 44, 51, 255);
static const ImU32 C_ACCENT = IM_COL32(226, 70, 48, 255);
static const ImU32 C_ACCENT_HOV = IM_COL32(242, 92, 70, 255);
static const ImU32 C_TEXT = IM_COL32(236, 236, 238, 255);
static const ImU32 C_DIM = IM_COL32(150, 156, 166, 255);
static const ImU32 C_LINE = IM_COL32(48, 52, 60, 255);
static const ImU32 C_GREEN = IM_COL32(76, 196, 120, 255);
static const ImU32 C_YELLOW = IM_COL32(236, 186, 64, 255);
static const ImU32 C_RED = IM_COL32(226, 80, 70, 255);

static ImVec4 V(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

static void LoadFonts()
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    char win[MAX_PATH]; GetWindowsDirectoryA(win, MAX_PATH);
    std::string fd = std::string(win) + "\\Fonts\\";
    auto add = [&](const char* file) -> ImFont* {
        std::string p = fd + file;
        if (GetFileAttributesA(p.c_str()) == INVALID_FILE_ATTRIBUTES) return nullptr;
        return io.Fonts->AddFontFromFileTTF(p.c_str(), 17.0f);
    };
    fReg = add("segoeui.ttf");
    if (!fReg) fReg = io.Fonts->AddFontDefault();
    fSemi = add("seguisb.ttf"); if (!fSemi) fSemi = fReg;
    fBold = add("segoeuib.ttf"); if (!fBold) fBold = fSemi;
    io.FontDefault = fReg;
}

static void ApplyStyle()
{
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    ImGui::StyleColorsDark(&st);
    st.WindowPadding = ImVec2(0, 0);
    st.FramePadding = ImVec2(12, 8);
    st.ItemSpacing = ImVec2(10, 10);
    st.ItemInnerSpacing = ImVec2(8, 6);
    st.FrameRounding = 7;
    st.ChildRounding = 0;
    st.PopupRounding = 10;
    st.WindowRounding = 12;
    st.ScrollbarSize = 10;
    st.ScrollbarRounding = 8;
    st.GrabRounding = 6;
    st.WindowBorderSize = 0;
    st.ChildBorderSize = 0;
    st.PopupBorderSize = 1;
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = V(C_BG);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = V(IM_COL32(28, 30, 35, 255));
    c[ImGuiCol_Border] = V(C_LINE);
    c[ImGuiCol_Text] = V(C_TEXT);
    c[ImGuiCol_TextDisabled] = V(C_DIM);
    c[ImGuiCol_FrameBg] = V(IM_COL32(36, 39, 46, 255));
    c[ImGuiCol_FrameBgHovered] = V(IM_COL32(44, 48, 56, 255));
    c[ImGuiCol_FrameBgActive] = V(IM_COL32(50, 54, 63, 255));
    c[ImGuiCol_Button] = V(IM_COL32(42, 46, 54, 255));
    c[ImGuiCol_ButtonHovered] = V(IM_COL32(54, 59, 69, 255));
    c[ImGuiCol_ButtonActive] = V(IM_COL32(62, 68, 80, 255));
    c[ImGuiCol_Header] = V(IM_COL32(42, 46, 54, 255));
    c[ImGuiCol_HeaderHovered] = V(IM_COL32(54, 59, 69, 255));
    c[ImGuiCol_HeaderActive] = V(IM_COL32(62, 68, 80, 255));
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = V(IM_COL32(60, 65, 75, 255));
    c[ImGuiCol_ScrollbarGrabHovered] = V(IM_COL32(80, 86, 98, 255));
    c[ImGuiCol_ScrollbarGrabActive] = V(IM_COL32(95, 102, 115, 255));
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.6f);
    c[ImGuiCol_NavCursor] = V(C_ACCENT);
    c[ImGuiCol_CheckMark] = V(C_ACCENT);
    c[ImGuiCol_TextSelectedBg] = V(IM_COL32(226, 70, 48, 110));
    st.ScaleAllSizes(S);
    st.FontSizeBase = 17.0f * S;
    st.FontScaleDpi = 1.0f;
}

// ------------------------------------------------------------------ selection logic
static int L() { return g_set.lang; }
static const char* T(int id) { return TXT[id][L()]; }

static const char* EX_CONFIRM[3] = {"Продолжить?", "Continue?", "¿Continuar?"};
static const char* EX_YES[3] = {"Да", "Yes", "Sí"};
static const char* EX_NO_NIGHT[3] = {"У этой карты нет ночной версии", "This map has no night version", "Este mapa no tiene versión nocturna"};
static const char* EX_FAV_T[3] = {"Сохранённые карты", "Saved maps", "Mapas guardados"};
static const char* EX_FAV_D[3] = {"Запомните выбранную карту, чтобы потом включать её одним нажатием.", "Remember the selected map to switch to it later with one click.", "Guarda el mapa elegido para activarlo luego con un clic."};
static const char* EX_FAV_ADD[3] = {"+  Сохранить выбранную карту", "+  Save the selected map", "+  Guardar el mapa elegido"};
static const char* EX_FAV_DEL[3] = {"Удалить", "Remove", "Quitar"};
static const char* EX_START_T[3] = {"Карта при запуске игры", "Map at game start", "Mapa al iniciar el juego"};
static const char* EX_START_D[3] = {"Включается сама каждый раз, когда игра запускается.", "Switched on automatically every time the game starts.", "Se activa sola cada vez que se inicia el juego."};
static const char* EX_START_SET[3] = {"Поставить выбранную", "Use the selected one", "Usar la elegida"};
static const char* EX_DEFAULT_SUF[3] = {"  (по умолчанию)", "  (default)", "  (predeterminado)"};
static const char* EX_CODE_T[3] = {"Карта по коду", "Map by code", "Mapa por código"};
static const char* EX_CODE_D[3] = {"Если нужной карты нет в списке — впишите её внутренний код.", "If the map is not in the list, type its internal code.", "Si el mapa no está en la lista, escribe su código interno."};
static const char* EX_CODE_GO[3] = {"Включить", "Apply", "Aplicar"};
static const char* EX_NO[3] = {"Нет", "No", "No"};
static const char* EX_NOW[3] = {"Сейчас в игре", "Now in game", "Ahora en el juego"};
static const char* EX_MUTE[3] = {"Выключать звук игры, пока она свёрнута", "Mute the game while it is minimized", "Silenciar el juego mientras está minimizado"};
static const char* EX_OVERLAY_T[3] = {"Оверлей в игре", "In-game overlay", "Superposición en el juego"};
static const char* EX_OVERLAY_ON[3] = {"Открывать лаунчер прямо в игре", "Open the launcher right inside the game", "Abrir el lanzador dentro del juego"};
static const char* EX_OVERLAY_KEY[3] = {"Сочетание клавиш:", "Hotkey:", "Atajo:"};
static const char* EX_PRESS_KEYS[3] = {"Нажмите сочетание…", "Press the keys…", "Pulsa las teclas…"};
static const char* EX_OVERLAY_HINT[3] = {"Включается и выключается сразу, даже во время игры. Esc — отменить ввод сочетания.",
                                         "Turns on and off right away, even while playing. Esc cancels key input.",
                                         "Se activa y desactiva al instante, incluso jugando. Esc cancela la entrada."};
static const char* EX_LINKS[3] = {"Полезные ссылки", "Useful links", "Enlaces útiles"};
struct LinkItem { const char* title[3]; const char* url; };
static const LinkItem LINKS[] = {
    {{"Скачать игру", "Download the game", "Descargar el juego"}, "https://www.mediafire.com/file/5wq0ooues6haxvh/Loadout.zip/file"},
    {{"Указать путь к папке с игрой", "Set the game folder", "Indicar la carpeta del juego"}, "pick:"},
    {{"Discord-сервер", "Discord server", "Servidor de Discord"}, "https://discord.gg/maMcHa2dzS"},
    {{"Проект Loadout Reloaded", "Loadout Reloaded project", "Proyecto Loadout Reloaded"}, "https://gitlab.com/Bedebao/loadout-reloaded"},
    {{"Автор этого лаунчера", "Author of this launcher", "Autor de este lanzador"}, "https://github.com/Leshugan"},
};

static bool StartsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }

static const ModeInfo* FindMode(const std::string& suffix)
{
    for (int i = 0; i < MODES_N; i++) if (suffix == MODES[i].suffix) return &MODES[i];
    return nullptr;
}

static std::vector<std::string> ModesFor(int base, bool night)
{
    const BaseMap& b = BASE_MAPS[base];
    std::string prefix = night ? b.nightPrefix : b.code;
    std::vector<std::string> found;
    for (int i = 0; i < ALL_MAPS_N; i++) {
        std::string m = ALL_MAPS[i];
        if (m == "drillcavern_beta_kc") continue;
        if (m == prefix) { found.push_back(""); continue; }
        if (!StartsWith(m, prefix + "_")) continue;
        if (!night && b.nightPrefix && m.find("night") != std::string::npos) continue;
        found.push_back(m.substr(prefix.size() + 1));
    }
    std::vector<std::string> out;
    for (int i = 0; i < MODES_N; i++)
        for (auto& f : found) if (f == MODES[i].suffix) out.push_back(f);
    for (auto& f : found) if (!FindMode(f)) out.push_back(f);
    return out;
}

static std::string Compose(int base, bool night, const std::string& suffix)
{
    const BaseMap& b = BASE_MAPS[base];
    if (b.single) return b.code;
    std::string p = night ? b.nightPrefix : b.code;
    return suffix.empty() ? p : p + "_" + suffix;
}

// returns base index or -1
static int ParseCode(const std::string& code, bool* night, std::string* suffix)
{
    for (int i = 0; i < BASE_MAPS_N; i++) {
        const BaseMap& b = BASE_MAPS[i];
        if (b.single) { if (code == b.code) { *night = false; suffix->clear(); return i; } continue; }
        if (b.nightPrefix) {
            std::string np = b.nightPrefix;
            if (code == np) { *night = true; suffix->clear(); return i; }
            if (StartsWith(code, np + "_")) { *night = true; *suffix = code.substr(np.size() + 1); return i; }
        }
        std::string dp = b.code;
        if (code == dp) { *night = false; suffix->clear(); return i; }
        if (StartsWith(code, dp + "_")) { *night = false; *suffix = code.substr(dp.size() + 1); return i; }
    }
    return -1;
}

// current selection
static int selBase = -1;
static int selNight = -1;      // -1 not chosen, 0 day, 1 night
static std::string selMode;
static bool selModeChosen = false;
static std::string selCustom;  // raw code not matching our list
static int step = 0;           // 0 map, 1 time, 2 mode
static std::string finalCode;
static char customBuf[64] = "";
static char serverBuf[64] = "";
static int flashId = -1; static double flashUntil = 0; static ImU32 flashCol = 0;
static std::string customErr;

static void Flash(int id, ImU32 col) { flashId = id; flashCol = col; flashUntil = ImGui::GetTime() + 4.0; }

static bool SelectionComplete() { return !finalCode.empty(); }

static std::string CodeTitle(const std::string& code)   // "Four Points · День · Deathsnatch"
{
    bool night; std::string suf;
    int b = ParseCode(code, &night, &suf);
    if (b < 0) return code;
    const BaseMap& m = BASE_MAPS[b];
    std::string t = m.name;
    if (m.single) return t;
    t += std::string("  ·  ") + T(night ? T_NIGHT : T_DAY);
    const ModeInfo* mi = FindMode(suf);
    t += std::string("  ·  ") + (mi ? (mi->name ? mi->name : T(T_NO_MODE)) : suf.c_str());
    return t;
}

static void CommitSelection(const std::string& code)
{
    finalCode = code;
    bool sent = g_host.writeMap(code);
    Flash(sent ? T_SENT : T_PENDING, sent ? C_GREEN : C_YELLOW);
}

static void LoadCode(const std::string& code)
{
    bool night; std::string suf;
    int b = ParseCode(code, &night, &suf);
    selCustom.clear();
    if (b >= 0) {
        selBase = b; selNight = night ? 1 : 0; selMode = suf; selModeChosen = true;
        step = BASE_MAPS[b].single ? 0 : 2;
    } else {
        selBase = -1; selNight = -1; selMode.clear(); selModeChosen = false; selCustom = code; step = 0;
    }
    CommitSelection(code);
}


// MediaFire: every page load gives a fresh direct link — fetch the page and start the download right away
std::string FetchMediafireDirect(const char* pageUrl)
{
    std::wstring url = U2W(pageUrl);
    URL_COMPONENTS uc{}; uc.dwStructSize = sizeof(uc);
    wchar_t host[256], path[2048];
    uc.lpszHostName = host; uc.dwHostNameLength = 256; uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return {};
    std::string body;
    HINTERNET ses = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36",
                                WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) ses = WinHttpOpen(L"Mozilla/5.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) return {};
    HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0);
    HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    if (req && WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(req, nullptr)) {
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(req, &avail) && avail) {
            std::string chunk(avail, 0); DWORD rd = 0;
            if (!WinHttpReadData(req, &chunk[0], avail, &rd) || !rd) break;
            body.append(chunk.data(), rd);
            if (body.size() > 4 * 1024 * 1024) break;
        }
    }
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    // newer pages hide the link: data-scrambled-url="<base64>"
    size_t sp = body.find("data-scrambled-url=\"");
    if (sp != std::string::npos) {
        sp += 20;
        size_t se = body.find('"', sp);
        std::string b64 = body.substr(sp, se - sp);
        DWORD n = 0;
        if (CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64, nullptr, &n, nullptr, nullptr) && n) {
            std::string out(n, 0);
            if (CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64, (BYTE*)&out[0], &n, nullptr, nullptr) && out.rfind("http", 0) == 0) return out.substr(0, n);
        }
    }
    // the blue "Download (size)" button: <a ... href="https://downloadNNN.mediafire.com/..." id="downloadButton">
    size_t p = body.find("https://download");
    while (p != std::string::npos) {
        size_t e = body.find_first_of("\"' <>", p);
        std::string u = body.substr(p, e == std::string::npos ? std::string::npos : e - p);
        if (u.find(".mediafire.com/") != std::string::npos) return u;
        p = body.find("https://download", p + 1);
    }
    return {};
}

#ifndef LL_OVERLAY
#include "setup.h"
#include "game.h"
static void ProgressBar(float frac, ImVec2 size);
#endif
static void OpenLink(const char* url)
{
    if (!strstr(url, "mediafire.com/file/")) { ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL); return; }
#ifndef LL_OVERLAY
    {   // ask where to put the archive, then download it ourselves; only a progress bar is shown
        static const char* PICK[3] = {"Куда скачать архив игры", "Where to save the game archive", "Dónde guardar el archivo del juego"};
        std::wstring d = SetupPickFolder(GetActiveWindow(), U2W(PICK[L()]).c_str(), SetupDownloadsDir());
        if (!d.empty()) SetupStartArchive(d);
    }
#else
    std::string page = url;
    std::thread([page]() {
        std::string direct = FetchMediafireDirect(page.c_str());
        ShellExecuteA(nullptr, "open", direct.empty() ? page.c_str() : direct.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }).detach();
#endif
}

// ------------------------------------------------------------------ widgets
static ImVec2 operator+(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }

static void TextAt(ImDrawList* dl, ImFont* f, float size, ImVec2 p, ImU32 col, const char* t, float wrap = 0)
{
    dl->AddText(f, size * S, p, col, t, nullptr, wrap);
}

static ImU32 PlaceholderColor(const char* code)
{
    unsigned h = 2166136261u; for (const char* c = code; *c; c++) h = (h ^ (unsigned char)*c) * 16777619u;
    static const ImU32 pal[] = {IM_COL32(58, 74, 96, 255), IM_COL32(86, 64, 52, 255), IM_COL32(52, 82, 72, 255),
                                IM_COL32(84, 58, 84, 255), IM_COL32(74, 78, 50, 255), IM_COL32(60, 64, 88, 255)};
    return pal[h % 6];
}

static void DrawPlaceholder(ImDrawList* dl, ImVec2 a, ImVec2 b, const char* code, const char* name, float rounding, ImDrawFlags fl)
{
    ImU32 c = PlaceholderColor(code);
    dl->AddRectFilled(a, b, c, rounding, fl);
    // soft diagonal stripes
    dl->PushClipRect(a, b, true);
    float w = b.x - a.x, h = b.y - a.y;
    for (float x = -h; x < w; x += 22 * S)
        dl->AddLine(ImVec2(a.x + x, b.y), ImVec2(a.x + x + h, a.y), IM_COL32(255, 255, 255, 14), 6 * S);
    std::string up = name; for (auto& ch : up) ch = (char)toupper((unsigned char)ch);
    size_t br = up.find(" (");
    if (br != std::string::npos) up = up.substr(0, br);
    float fs = 30 * S;
    ImVec2 ts = fBold->CalcTextSizeA(fs, FLT_MAX, 0, up.c_str());
    if (ts.x > w - 20 * S) { fs *= (w - 20 * S) / ts.x; ts = fBold->CalcTextSizeA(fs, FLT_MAX, 0, up.c_str()); }
    dl->AddText(fBold, fs, ImVec2(a.x + (w - ts.x) / 2, a.y + (h - ts.y) / 2), IM_COL32(255, 255, 255, 70), up.c_str());
    dl->PopClipRect();
}

static void Pill(ImDrawList* dl, ImVec2 topRight, const char* text, ImU32 bg, ImU32 fg)
{
    ImVec2 ts = fSemi->CalcTextSizeA(13 * S, FLT_MAX, 0, text);
    ImVec2 a(topRight.x - ts.x - 16 * S, topRight.y), b(topRight.x, topRight.y + ts.y + 6 * S);
    dl->AddRectFilled(a, b, bg, 20 * S);
    dl->AddText(fSemi, 13 * S, ImVec2(a.x + 8 * S, a.y + 3 * S), fg, text);
}

// image card with title + subtitle
static bool ImageCard(const char* id, ImTextureID tex, const char* code, float w, const char* title, const char* sub, bool pve, bool selected, bool now, ImTextureID tex2 = 0)
{
    float imgH = std::floor(w * 9.0f / 16.0f);
    float textH = 62 * S;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size(w, imgH + textH);
    bool clicked = ImGui::InvisibleButton(id, size);
    bool hov = ImGui::IsItemHovered();
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = 10 * S;
    dl->AddRectFilled(p, p + size, hov ? C_CARD_HOV : C_CARD, r);
    ImVec2 ia = p, ib = p + ImVec2(w, imgH);
    if (tex) {
        float zoom = hov ? 0.03f : 0.0f;
        dl->AddImageRounded(ImTextureRef(tex), ia, ib, ImVec2(zoom, zoom), ImVec2(1 - zoom, 1 - zoom), IM_COL32_WHITE, r, ImDrawFlags_RoundCornersTop);
        if (tex2) {
            // day/night variants: smooth cross-fade, 3.5 s each
            double t = std::fmod(ImGui::GetTime() + (code[0] % 7) * 0.9, 8.0);
            float a = t < 3.3 ? 0.0f : t < 4.0 ? (float)((t - 3.3) / 0.7) : t < 7.3 ? 1.0f : (float)(1.0 - (t - 7.3) / 0.7);
            a = a * a * (3 - 2 * a);
            if (a > 0.01f) dl->AddImageRounded(ImTextureRef(tex2), ia, ib, ImVec2(zoom, zoom), ImVec2(1 - zoom, 1 - zoom), IM_COL32(255, 255, 255, (int)(a * 255)), r, ImDrawFlags_RoundCornersTop);
            const char* lbl = a < 0.5f ? TXT[T_DAY][L()] : TXT[T_NIGHT][L()];
            ImVec2 ts = fSemi->CalcTextSizeA(13 * S, FLT_MAX, 0, lbl);
            ImVec2 pa(ia.x + 8 * S, ia.y + 8 * S), pb(pa.x + ts.x + 16 * S, pa.y + ts.y + 6 * S);
            dl->AddRectFilled(pa, pb, IM_COL32(20, 22, 26, 210), 20 * S);
            dl->AddText(fSemi, 13 * S, ImVec2(pa.x + 8 * S, pa.y + 3 * S), a < 0.5f ? IM_COL32(250, 210, 110, 255) : IM_COL32(140, 180, 255, 255), lbl);
        }
    } else {
        DrawPlaceholder(dl, ia, ib, code, title, r, ImDrawFlags_RoundCornersTop);
    }
    // darken bottom of image
    dl->AddRectFilledMultiColor(ImVec2(ia.x, ib.y - 30 * S), ib, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 90), IM_COL32(0, 0, 0, 90));
    float tx = p.x + 12 * S;
    TextAt(dl, fBold, 17, ImVec2(tx, ib.y + 8 * S), C_TEXT, title);
    dl->PushClipRect(ImVec2(p.x, ib.y), p + size, true);
    TextAt(dl, fReg, 14, ImVec2(tx, ib.y + 32 * S), C_DIM, sub, w - 24 * S);
    dl->PopClipRect();
    float py = p.y + 8 * S;
    if (pve) { Pill(dl, ImVec2(p.x + w - 8 * S, py), "PvE", IM_COL32(20, 22, 26, 210), C_GREEN); }
    if (now) { Pill(dl, ImVec2(p.x + w - 8 * S, py + (pve ? 26 * S : 0)), EX_NOW[L()], IM_COL32(20, 22, 26, 210), C_YELLOW); }
    if (selected) dl->AddRect(p, p + size, C_ACCENT, r, 0, 3 * S);
    else if (hov) dl->AddRect(p, p + size, IM_COL32(255, 255, 255, 60), r, 0, 1.5f * S);
    if (pve && hov && ImGui::BeginTooltip()) { ImGui::TextUnformatted(T(T_PVE_TIP)); ImGui::EndTooltip(); }
    return clicked;
}

static bool TextCard(const char* id, float w, float h, const char* title, const char* sub, const char* tag, ImU32 tagCol, bool selected, bool now)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size(w, h);
    bool clicked = ImGui::InvisibleButton(id, size);
    bool hov = ImGui::IsItemHovered();
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = 10 * S;
    dl->AddRectFilled(p, p + size, hov ? C_CARD_HOV : C_CARD, r);
    dl->AddRectFilled(p, ImVec2(p.x + 5 * S, p.y + h), selected ? C_ACCENT : IM_COL32(60, 65, 75, 255), r, ImDrawFlags_RoundCornersLeft);
    TextAt(dl, fBold, 18, ImVec2(p.x + 20 * S, p.y + 12 * S), C_TEXT, title);
    dl->PushClipRect(p, p + size, true);
    TextAt(dl, fReg, 14.5f, ImVec2(p.x + 20 * S, p.y + 40 * S), C_DIM, sub, w - 36 * S);
    dl->PopClipRect();
    float py = p.y + 10 * S;
    if (tag) { Pill(dl, ImVec2(p.x + w - 10 * S, py), tag, IM_COL32(20, 22, 26, 220), tagCol); py += 26 * S; }
    if (now) Pill(dl, ImVec2(p.x + w - 10 * S, py), EX_NOW[L()], IM_COL32(20, 22, 26, 220), C_YELLOW);
    if (selected) dl->AddRect(p, p + size, C_ACCENT, r, 0, 2.5f * S);
    else if (hov) dl->AddRect(p, p + size, IM_COL32(255, 255, 255, 60), r, 0, 1.5f * S);
    return clicked;
}

static bool AccentButton(const char* label, ImVec2 size, bool enabled = true)
{
    ImGui::PushStyleColor(ImGuiCol_Button, V(C_ACCENT));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, V(C_ACCENT_HOV));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, V(IM_COL32(200, 58, 40, 255)));
    ImGui::PushStyleColor(ImGuiCol_Text, V(IM_COL32_WHITE));
    ImGui::BeginDisabled(!enabled);
    bool r = ImGui::Button(label, size);
    ImGui::EndDisabled();
    ImGui::PopStyleColor(4);
    return r;
}

static void SectionTitle(const char* t)
{
    ImGui::PushFont(fBold, 15 * S);
    ImGui::PushStyleColor(ImGuiCol_Text, V(C_DIM));
    std::string up = t;
    ImGui::TextUnformatted(up.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

// ------------------------------------------------------------------ screens
static std::string NowInGame() { return g_host.currentMap(); }

static void DrawMapGrid()
{
    float avail = ImGui::GetContentRegionAvail().x;
    float gap = 16 * S;
    int cols = (int)((avail + gap) / (250 * S + gap)); if (cols < 1) cols = 1;
    float w = std::floor((avail - gap * (cols - 1)) / cols);
    std::string now = NowInGame();
    bool nNight; std::string nSuf; int nowBase = now.empty() ? -1 : ParseCode(now, &nNight, &nSuf);

    for (int sec = 0; sec < 2; sec++) {
        if (sec == 1) ImGui::Dummy(ImVec2(0, 10 * S));
        SectionTitle(T(sec == 0 ? T_SEC_MAIN : T_SEC_OTHER));
        ImGui::Dummy(ImVec2(0, 2 * S));
        int col = 0;
        for (int i = 0; i < BASE_MAPS_N; i++) {
            const BaseMap& b = BASE_MAPS[i];
            if (b.main != (sec == 0)) continue;
            if (col > 0) ImGui::SameLine(0, gap);
            ImGui::PushID(i);
            if (ImageCard("card", TexId(b.img), b.code, w, b.name, b.desc[L()], b.pve, selBase == i && selCustom.empty(), nowBase == i, b.nightPrefix ? TexId(b.imgNight) : 0)) {
                selBase = i; selNight = -1; selMode.clear(); selModeChosen = false; selCustom.clear(); finalCode.clear();
                if (b.single) CommitSelection(b.code);
                else step = 1;
            }
            ImGui::PopID();
            col = (col + 1) % cols;
            if (col == 0) ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
        }
        if (col != 0) ImGui::Dummy(ImVec2(0, 0));
    }
    ImGui::Dummy(ImVec2(0, 16 * S));
}

static void DrawTimeStep()
{
    const BaseMap& b = BASE_MAPS[selBase];
    float avail = ImGui::GetContentRegionAvail().x;
    float gap = 20 * S;
    float w = std::floor((avail - gap) / 2); if (w > 560 * S) w = 560 * S;
    for (int n = 0; n < 2; n++) {
        if (n) ImGui::SameLine(0, gap);
        ImGui::PushID(n);
        bool none = n == 1 && !b.nightPrefix;     // this map has no night version
        std::string title = std::string(b.name) + " — " + T(n ? T_NIGHT : T_DAY);
        ImGui::BeginDisabled(none);
        if (ImageCard("t", none ? 0 : TexId(n ? b.imgNight : b.img), b.code, w, title.c_str(), none ? EX_NO_NIGHT[L()] : b.desc[L()], false, selNight == n, false) && !none) {
            selNight = n; selMode.clear(); selModeChosen = false; finalCode.clear();
            step = 2;
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
}

static void DrawModeStep()
{
    const BaseMap& b = BASE_MAPS[selBase];
    bool night = selNight == 1;
    std::vector<std::string> modes = ModesFor(selBase, night);
    float avail = ImGui::GetContentRegionAvail().x;
    float gap = 14 * S;
    int cols = (int)((avail + gap) / (300 * S + gap)); if (cols < 1) cols = 1; if (cols > 3) cols = 3;
    float w = std::floor((avail - gap * (cols - 1)) / cols);
    float h = 92 * S;
    std::string now = NowInGame();
    // preview strip
    ImTextureID tex = TexId(night ? b.imgNight : b.img);
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float ph = 120 * S;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 a = p, bb = p + ImVec2(avail, ph);
        if (tex) {
            float aspect = avail / ph; // crop center of 16:9 image
            float v = (16.0f / 9.0f) / aspect; float v0 = 0.5f - v / 2, v1 = 0.5f + v / 2;
            dl->AddImageRounded(ImTextureRef(tex), a, bb, ImVec2(0, v0), ImVec2(1, v1), IM_COL32(255, 255, 255, 255), 10 * S);
        } else DrawPlaceholder(dl, a, bb, b.code, b.name, 10 * S, 0);
        dl->AddRectFilledMultiColor(a, bb, IM_COL32(0, 0, 0, 170), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 170));
        std::string t = b.name;
        if (b.nightPrefix) t += std::string(" — ") + T(night ? T_NIGHT : T_DAY);
        TextAt(dl, fBold, 26, ImVec2(a.x + 20 * S, a.y + ph / 2 - 30 * S), C_TEXT, t.c_str());
        TextAt(dl, fReg, 15, ImVec2(a.x + 20 * S, a.y + ph / 2 + 6 * S), IM_COL32(220, 220, 225, 255), b.desc[L()]);
        ImGui::Dummy(ImVec2(avail, ph));
        ImGui::Dummy(ImVec2(0, 4 * S));
    }
    int col = 0;
    for (size_t i = 0; i < modes.size(); i++) {
        const ModeInfo* m = FindMode(modes[i]);
        std::string name = m ? (m->name ? m->name : T(T_NO_MODE)) : modes[i];
        const char* desc = m ? m->desc[L()] : modes[i].c_str();
        bool pveMode = modes[i] == "botwave" || modes[i] == "botwaves";
        std::string code = Compose(selBase, night, modes[i]);
        if (col > 0) ImGui::SameLine(0, gap);
        ImGui::PushID((int)i);
        if (TextCard("m", w, h, name.c_str(), desc, pveMode ? "PvE" : nullptr, C_GREEN, selModeChosen && selMode == modes[i] && selCustom.empty(), code == now)) {
            selMode = modes[i]; selModeChosen = true; selCustom.clear();
            CommitSelection(code);
        }
        ImGui::PopID();
        col = (col + 1) % cols;
        if (col == 0) ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
    }
}

static void DrawSteps()
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool single = selBase >= 0 && BASE_MAPS[selBase].single;
    struct St { int id; int target; bool enabled; } st[3] = {
        {T_STEP_MAP, 0, true},
        {T_STEP_TIME, 1, selBase >= 0 && !single},
        {T_STEP_MODE, 2, selBase >= 0 && !single && selNight >= 0},
    };
    for (int i = 0; i < 3; i++) {
        if (i) {
            ImGui::SameLine(0, 6 * S);
            ImVec2 p = ImGui::GetCursorScreenPos();
            float y = p.y + ImGui::GetFrameHeight() / 2;
            dl->AddLine(ImVec2(p.x, y), ImVec2(p.x + 22 * S, y), C_LINE, 2 * S);
            ImGui::Dummy(ImVec2(22 * S, 1));
            ImGui::SameLine(0, 6 * S);
        }
        bool active = step == st[i].target;
        char lbl[128]; snprintf(lbl, sizeof(lbl), "%d  %s##st%d", i + 1, T(st[i].id), i);
        ImGui::PushFont(fSemi, 16 * S);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 20 * S);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, V(C_ACCENT));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, V(C_ACCENT_HOV));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, V(C_ACCENT));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, V(IM_COL32(32, 35, 41, 255)));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, V(IM_COL32(44, 48, 56, 255)));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, V(IM_COL32(50, 54, 63, 255)));
        }
        ImGui::BeginDisabled(!st[i].enabled);
        if (ImGui::Button(lbl) && st[i].enabled) step = st[i].target;
        ImGui::EndDisabled();
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
        ImGui::PopFont();
    }
}

static void DrawSummaryRow(const char* label, const char* value)
{
    ImGui::PushStyleColor(ImGuiCol_Text, V(C_DIM));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine(110 * S);
    ImGui::PushFont(fSemi, 16 * S);
    ImGui::TextUnformatted(value);
    ImGui::PopFont();
}

static bool g_openConfirmStart = false;
static bool g_openSettings = false;
static bool g_openLinks = false;
static double g_copiedUntil = 0;

static void DrawRightPanel(float width)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float pad = 20 * S;
    ImGui::SetCursorPos(ImVec2(pad, pad));
    ImGui::BeginGroup();
    float w = width - pad * 2;
    ImGui::PushItemWidth(w);

    // preview
    ImVec2 p = ImGui::GetCursorScreenPos();
    float ph = std::floor(w * 9 / 16);
    const BaseMap* b = selBase >= 0 && selCustom.empty() ? &BASE_MAPS[selBase] : nullptr;
    const char* imgName = b ? (selNight == 1 ? b->imgNight : b->img) : nullptr;
    if (b && !strcmp(b->code, "gliese_581") && selNight != 1 && selModeChosen && selMode == "mu") imgName = "trailerpark_mu";
    ImTextureID tex = TexId(imgName);
    if (tex) dl->AddImageRounded(ImTextureRef(tex), p, p + ImVec2(w, ph), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 10 * S);
    else if (b) DrawPlaceholder(dl, p, p + ImVec2(w, ph), b->code, b->name, 10 * S, 0);
    else {
        dl->AddRectFilled(p, p + ImVec2(w, ph), C_CARD, 10 * S);
        const char* t = selCustom.empty() ? T(T_CHOOSE_MAP) : selCustom.c_str();
        ImVec2 ts = fSemi->CalcTextSizeA(16 * S, FLT_MAX, 0, t);
        dl->AddText(fSemi, 16 * S, ImVec2(p.x + (w - ts.x) / 2, p.y + (ph - ts.y) / 2), C_DIM, t);
    }
    ImGui::Dummy(ImVec2(w, ph));
    ImGui::Dummy(ImVec2(0, 2 * S));

    // summary
    if (b) {
        DrawSummaryRow(T(T_MAP), b->name);
        if (!b->single) {
            DrawSummaryRow(T(T_TIME), selNight < 0 ? "—" : T(selNight ? T_NIGHT : T_DAY));
            std::string mn = "—";
            if (selModeChosen) { const ModeInfo* m = FindMode(selMode); mn = m ? (m->name ? m->name : T(T_NO_MODE)) : selMode; }
            DrawSummaryRow(T(T_MODE), mn.c_str());
        }
    } else if (!selCustom.empty()) {
        DrawSummaryRow(T(T_MAP), T(T_CUSTOM_CODE));
    }
    if (SelectionComplete()) {
        ImGui::PushStyleColor(ImGuiCol_Text, V(C_DIM));
        ImGui::TextUnformatted(T(T_MAP_CODE));
        ImGui::PopStyleColor();
        ImGui::SameLine(110 * S);
        ImGui::PushFont(fReg, 15 * S);
        ImGui::PushStyleColor(ImGuiCol_Text, V(IM_COL32(150, 156, 166, 255)));
        ImGui::TextUnformatted(finalCode.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemClicked()) { ImGui::SetClipboardText(finalCode.c_str()); g_copiedUntil = ImGui::GetTime() + 1.5; }
        if (ImGui::GetTime() < g_copiedUntil) { ImGui::SameLine(); ImGui::TextColored(V(C_GREEN), "%s", T(T_COPIED)); }
    }
    ImGui::Dummy(ImVec2(0, 4 * S));

    // main action
    int gs = g_host.gameState();
    bool running = gs == GS_READY || gs == GS_WAIT_INIT || gs == GS_LAUNCHING;
    if (SelectionComplete()) {
        ImGui::PushFont(fBold, 19 * S);
        if (running) {
            if (AccentButton(T(T_RETURN), ImVec2(w, 54 * S))) {
                if (g_host.writeMap(finalCode)) Flash(T_SENT, C_GREEN);
                g_host.returnToGame();
            }
        } else if (gs != GS_NOEXE) {
            if (AccentButton(T(T_BTN_START), ImVec2(w, 54 * S))) g_host.launchGame();
        }
        ImGui::PopFont();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, V(C_DIM));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
        ImGui::TextUnformatted(T(T_INCOMPLETE));
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    if (flashId >= 0 && ImGui::GetTime() < flashUntil) {
        ImGui::PushStyleColor(ImGuiCol_Text, V(flashCol));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
        ImGui::TextUnformatted(T(flashId));
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    if (SelectionComplete() && running) {
        ImGui::PushFont(fReg, 14 * S);
        ImGui::PushStyleColor(ImGuiCol_Text, V(C_DIM));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
        ImGui::TextUnformatted(T(T_HINT_APPLY));
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    float sp = ImGui::GetStyle().ItemSpacing.x;
    auto Section = [&](const char* title, const char* desc) {
        ImGui::Dummy(ImVec2(0, 10 * S));
        { ImVec2 lp = ImGui::GetCursorScreenPos(); dl->AddLine(lp, ImVec2(lp.x + w, lp.y), C_LINE, 1); }
        ImGui::Dummy(ImVec2(0, 8 * S));
        ImGui::PushFont(fBold, 17 * S); ImGui::TextUnformatted(title); ImGui::PopFont();
        ImGui::PushFont(fReg, 14 * S);
        ImGui::PushStyleColor(ImGuiCol_Text, V(C_DIM));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 2 * S));
    };
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));

    // saved maps: one click switches to a saved map
    Section(EX_FAV_T[L()], EX_FAV_D[L()]);
    float delW = ImGui::GetFrameHeight() * 1.6f;
    bool addShown = false;
    for (int i = 0; i < 3; i++) {
        ImGui::PushID(i);
        std::string fav; { std::lock_guard<std::mutex> lk(g_setMu); fav = g_set.fav[i]; }
        if (!fav.empty()) {
            std::string lbl = CodeTitle(fav);
            bool cur = fav == finalCode;
            if (cur) ImGui::PushStyleColor(ImGuiCol_Border, V(C_ACCENT));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, cur ? 1.5f * S : 0.0f);
            if (ImGui::Button(lbl.c_str(), ImVec2(w - delW - sp, 0))) LoadCode(fav);
            ImGui::PopStyleVar();
            if (cur) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("%s", fav.c_str()); }
            ImGui::SameLine();
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
            if (ImGui::Button("X", ImVec2(delW, 0))) {
                { std::lock_guard<std::mutex> lk(g_setMu); g_set.fav[i].clear(); }
                SettingsSave();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", EX_FAV_DEL[L()]);
            ImGui::PopStyleVar();
        } else if (!addShown) {          // one "save" button in the first free slot
            addShown = true;
            bool already = false;
            { std::lock_guard<std::mutex> lk(g_setMu); for (auto& f : g_set.fav) if (!finalCode.empty() && f == finalCode) already = true; }
            ImGui::BeginDisabled(!SelectionComplete() || already);
            if (ImGui::Button(EX_FAV_ADD[L()], ImVec2(w, 0))) {
                { std::lock_guard<std::mutex> lk(g_setMu); g_set.fav[i] = finalCode; }
                SettingsSave();
                Flash(T_SAVED, C_GREEN);
            }
            ImGui::EndDisabled();
        }
        ImGui::PopID();
    }

    // map the game starts on
    Section(EX_START_T[L()], EX_START_D[L()]);
    std::string sm; { std::lock_guard<std::mutex> lk(g_setMu); sm = g_set.startMap; }
    bool smDefault = sm.empty() || sm == "shooting_gallery_solo";
    {
        std::string t = smDefault ? CodeTitle("shooting_gallery_solo") + EX_DEFAULT_SUF[L()] : CodeTitle(sm);
        ImGui::PushFont(fSemi, 16 * S);
        if (smDefault) ImGui::TextUnformatted(t.c_str()); else ImGui::TextColored(V(C_YELLOW), "%s", t.c_str());
        ImGui::PopFont();
    }
    float halfW = (w - sp) / 2;
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
    ImGui::BeginDisabled(!SelectionComplete() || finalCode == sm);
    if (ImGui::Button(EX_START_SET[L()], ImVec2(halfW, 0))) g_openConfirmStart = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(smDefault);
    if (ImGui::Button(T(T_RESET), ImVec2(halfW, 0))) {
        { std::lock_guard<std::mutex> lk(g_setMu); g_set.startMap.clear(); }
        SettingsSave();
    }
    ImGui::EndDisabled();
    ImGui::PopStyleVar();

    // any map by its internal code
    Section(EX_CODE_T[L()], EX_CODE_D[L()]);
    float goW = 110 * S;
    ImGui::SetNextItemWidth(w - goW - sp);
    bool enter = ImGui::InputTextWithHint("##custom", T(T_CUSTOM_HINT), customBuf, sizeof(customBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsNoBlank);
    ImGui::SameLine();
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
    if (ImGui::Button(EX_CODE_GO[L()], ImVec2(goW, 0)) || enter) {
        std::string c = customBuf;
        if (c.size() < 4) customErr = T(T_TOO_SHORT);
        else if (c.size() > 50) customErr = T(T_TOO_LONG);
        else {
            customErr.clear();
            { std::lock_guard<std::mutex> lk(g_setMu); g_set.customMap = c; }
            SettingsSave();
            LoadCode(c);
        }
    }
    ImGui::PopStyleVar();
    if (!customErr.empty()) ImGui::TextColored(V(C_RED), "%s", customErr.c_str());
    ImGui::PopStyleVar();

    ImGui::PopItemWidth();
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(0, pad));
}

static void DrawPopups()
{
    if (g_openConfirmStart) { ImGui::OpenPopup("confirm_start"); g_openConfirmStart = false; }
    if (g_openSettings) {
        ImGui::OpenPopup("settings");
        g_openSettings = false;
        std::lock_guard<std::mutex> lk(g_setMu);
        strncpy(serverBuf, g_set.server.c_str(), sizeof(serverBuf) - 1);
    }
    if (g_openLinks) { ImGui::OpenPopup("links"); g_openLinks = false; }
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24 * S, 20 * S));
    if (ImGui::BeginPopupModal("links", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::PushFont(fBold, 20 * S); ImGui::TextUnformatted(EX_LINKS[L()]); ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 4 * S));
        static std::string s_pickMsg;
        for (int i = 0; i < (int)(sizeof(LINKS) / sizeof(LINKS[0])); i++) {
            bool pick = !strcmp(LINKS[i].url, "pick:");
#ifdef LL_OVERLAY
            if (pick) continue;      // the launcher can't move itself from inside the game
#endif
            ImGui::PushID(i);
            static const char* DL_SUB[3] = {"Архив игры, 2,2 ГБ — вы выберете, куда его сохранить", "Game archive, 2.2 GB — you choose where to save it", "Archivo del juego, 2,2 GB: tú eliges dónde guardarlo"};
            static const char* PK_SUB[3] = {"Если игра уже есть на компьютере — лаунчер переедет в её папку", "If the game is already on this PC, the launcher moves into its folder", "Si el juego ya está en el equipo, el lanzador se mueve a su carpeta"};
            const char* sub = strstr(LINKS[i].url, "mediafire.com") ? DL_SUB[L()] : pick ? PK_SUB[L()] : LINKS[i].url;
            if (TextCard("lnk", 420 * S, 70 * S, LINKS[i].title[L()], sub, nullptr, 0, false, false)) {
#ifndef LL_OVERLAY
                if (pick) {
                    static const char* TTL[3] = {"Папка с игрой Loadout", "Loadout game folder", "Carpeta del juego Loadout"};
                    static const char* NF[3] = {"В этой папке нет Loadout.exe", "There is no Loadout.exe in this folder", "No hay Loadout.exe en esta carpeta"};
                    static const char* SAME[3] = {"Лаунчер уже в этой папке", "The launcher is already in this folder", "El lanzador ya está en esta carpeta"};
                    static const char* FAIL[3] = {"Не удалось перенести лаунчер в эту папку", "Could not move the launcher into this folder", "No se pudo mover el lanzador a esta carpeta"};
                    s_pickMsg.clear();
                    std::wstring d = SetupPickFolder(GetActiveWindow(), U2W(TTL[L()]).c_str());
                    if (!d.empty()) {
                        std::wstring game = SetupFindGame(d);
                        if (game.empty()) s_pickMsg = NF[L()];
                        else if (_wcsicmp(game.c_str(), g_host.gameFolder.c_str()) == 0) s_pickMsg = SAME[L()];
                        else if (SetupMoveLauncher(game, false)) PostQuitMessage(0);
                        else s_pickMsg = FAIL[L()];
                    }
                } else
#endif
                OpenLink(LINKS[i].url);
            }
            ImGui::PopID();
#ifndef LL_OVERLAY
            if (i == 0) {
        {
            SetupStatus ar = SetupArchiveGet();
            if (ar.phase == ST_LINK || ar.phase == ST_DOWNLOADING || ar.phase == ST_DONE) {
                float fr = ar.phase == ST_DONE ? 1.0f : ar.total ? (float)((double)ar.got / (double)ar.total) : 0.0f;
                bool busy = ar.phase != ST_DONE;
                float cw = busy ? 110 * S : 0, fh = ImGui::GetFrameHeight();
                float barW = 420 * S - (busy ? cw + 10 * S : 0);
                ImVec2 c0 = ImGui::GetCursorPos();
                ImGui::SetCursorPos(ImVec2(c0.x, c0.y + (fh - 10 * S) / 2));
                ProgressBar(fr, ImVec2(barW, 10 * S));
                if (busy) {
                    static const char* CANCEL[3] = {"Отмена", "Cancel", "Cancelar"};
                    ImGui::SetCursorPos(ImVec2(c0.x + barW + 10 * S, c0.y));
                    if (ImGui::Button(CANCEL[L()], ImVec2(cw, 0))) SetupCancel();
                }
            }
        }
            }
            if (pick && !s_pickMsg.empty()) ImGui::TextColored(V(C_YELLOW), "%s", s_pickMsg.c_str());
#endif
        }
        ImGui::Dummy(ImVec2(0, 4 * S));
        if (ImGui::Button(T(T_CLOSE), ImVec2(420 * S, 0))) { ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("confirm_start", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::PushFont(fBold, 19 * S); ImGui::TextUnformatted(EX_START_T[L()]); ImGui::PopFont();
        ImGui::TextColored(V(C_YELLOW), "%s", CodeTitle(finalCode).c_str());
        ImGui::PushTextWrapPos(420 * S);
        ImGui::TextUnformatted(T(T_START_WARN));
        ImGui::PopTextWrapPos();
        ImGui::TextUnformatted(EX_CONFIRM[L()]);
        ImGui::Dummy(ImVec2(0, 4 * S));
        if (AccentButton(EX_YES[L()], ImVec2(140 * S, 0))) {
            { std::lock_guard<std::mutex> lk(g_setMu); g_set.startMap = finalCode; }
            SettingsSave();
            Flash(T_SAVED, C_GREEN);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(EX_NO[L()], ImVec2(140 * S, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::PushFont(fBold, 20 * S); ImGui::TextUnformatted(T(T_SETTINGS)); ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 4 * S));
        SectionTitle(T(T_SERVER));
        ImGui::SetNextItemWidth(320 * S);
        ImGui::InputText("##server", serverBuf, sizeof(serverBuf), ImGuiInputTextFlags_CharsNoBlank);
        ImGui::SameLine();
        if (ImGui::Button(T(T_DEFAULT))) strcpy(serverBuf, "api.loadout.rip");
        std::string sv = serverBuf;
        if (ValidServer(sv) != sv) ImGui::TextColored(V(C_YELLOW), "%s", T(T_SERVER_INVALID));
        ImGui::PushFont(fReg, 14 * S);
        ImGui::TextColored(V(C_DIM), "%s", T(T_SERVER_HINT));
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 6 * S));
        {
            bool mute; { std::lock_guard<std::mutex> lk(g_setMu); mute = g_set.muteMinimized; }
            if (ImGui::Checkbox(EX_MUTE[L()], &mute)) {
                { std::lock_guard<std::mutex> lk(g_setMu); g_set.muteMinimized = mute; }
                SettingsSave();
            }
        }
#ifndef LL_OVERLAY
        {
            static const char* UNC[3] = {"Патч без цензуры", "Uncensored patch", "Parche sin censura"};
            static const char* UNC_D[3] = {"Убирает мозаику с Axl's Rod и T-Bone's Steak (патч от Loadout Reloaded)", "Removes the mosaic from Axl's Rod and T-Bone's Steak (Loadout Reloaded patch)", "Quita el mosaico de Axl's Rod y T-Bone's Steak (parche de Loadout Reloaded)"};
            static const char* UNC_LATER[3] = {"Применится, когда игра будет закрыта", "Will apply when the game is closed", "Se aplicará al cerrar el juego"};
            static const char* UNC_ERR[3] = {"Не удалось изменить файл игры Data\\29911B90.ARC", "Could not change the game file Data\\29911B90.ARC", "No se pudo cambiar el archivo Data\\29911B90.ARC"};
            static int s_uncRes = 0;
            bool unc; { std::lock_guard<std::mutex> lk(g_setMu); unc = g_set.uncensored; }
            if (ImGui::Checkbox(UNC[L()], &unc)) {
                { std::lock_guard<std::mutex> lk(g_setMu); g_set.uncensored = unc; }
                SettingsSave();
                s_uncRes = GameApplyPatch();
            }
            ImGui::PushFont(fReg, 14 * S);
            ImGui::TextColored(V(C_DIM), "%s", UNC_D[L()]);
            if (s_uncRes == 1) ImGui::TextColored(V(C_YELLOW), "%s", UNC_LATER[L()]);
            else if (s_uncRes == 2) ImGui::TextColored(V(C_RED), "%s", UNC_ERR[L()]);
            ImGui::PopFont();
        }
#endif
        ImGui::Dummy(ImVec2(0, 6 * S));
        SectionTitle(EX_OVERLAY_T[L()]);
        {
            bool on; int m, k;
            { std::lock_guard<std::mutex> lk(g_setMu); on = g_set.overlayEnabled; m = g_set.overlayMods; k = g_set.overlayVk; }
            if (ImGui::Checkbox(EX_OVERLAY_ON[L()], &on)) {
                { std::lock_guard<std::mutex> lk(g_setMu); g_set.overlayEnabled = on; }
                SettingsSave();
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(EX_OVERLAY_KEY[L()]);
            ImGui::SameLine();
            std::string lbl = g_uiCaptureHotkey ? std::string(EX_PRESS_KEYS[L()]) : HotkeyName(m, k);
            lbl += "##hk";
            if (g_uiCaptureHotkey) { ImGui::PushStyleColor(ImGuiCol_Button, V(C_ACCENT)); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, V(C_ACCENT_HOV)); }
            if (ImGui::Button(lbl.c_str(), ImVec2(220 * S, 0))) g_uiCaptureHotkey = !g_uiCaptureHotkey;
            if (g_uiCaptureHotkey) ImGui::PopStyleColor(2);
            ImGui::PushFont(fReg, 14 * S);
            ImGui::PushTextWrapPos(480 * S);
            ImGui::TextColored(V(C_DIM), "%s", EX_OVERLAY_HINT[L()]);
            ImGui::PopTextWrapPos();
            ImGui::PopFont();
        }
        ImGui::Dummy(ImVec2(0, 6 * S));
        SectionTitle(T(T_GAME_FOLDER));
        {
            int n = WideCharToMultiByte(CP_UTF8, 0, g_host.gameFolder.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string d(n, 0); WideCharToMultiByte(CP_UTF8, 0, g_host.gameFolder.c_str(), -1, &d[0], n, nullptr, nullptr);
            ImGui::PushTextWrapPos(480 * S);
            ImGui::TextUnformatted(d.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::Dummy(ImVec2(0, 8 * S));
        if (AccentButton(T(T_SAVE), ImVec2(160 * S, 0))) {
            bool changed;
            { std::lock_guard<std::mutex> lk(g_setMu); std::string nv = ValidServer(sv); changed = nv != g_set.server; g_set.server = nv; }
            SettingsSave();
            int gs = g_host.gameState();
            if (changed && (gs == GS_READY || gs == GS_WAIT_INIT)) Flash(T_RESTART_NEEDED, C_YELLOW);
            else Flash(T_SAVED, C_GREEN);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(T(T_CLOSE), ImVec2(160 * S, 0))) { g_uiCaptureHotkey = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

static void DrawHeader(float width, float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 o = ImGui::GetWindowPos();
    dl->AddRectFilled(o, o + ImVec2(width, h), C_PANEL, g_host.overlay ? 12 * S : 0, ImDrawFlags_RoundCornersTop);
    dl->AddLine(o + ImVec2(0, h - 1), o + ImVec2(width, h - 1), C_LINE, 1);
    float pad = 20 * S;
    float ic = 40 * S;
    ImTextureID icon = TexId("icon");
    float lh = 56 * S, lw = lh * 864.0f / 471.0f;
    float up = g_host.overlay ? 0 : 6 * S;      // logo and caption sit a little higher
    if (icon) dl->AddImage(ImTextureRef(icon), o + ImVec2(pad - 3 * S, (h - lh) / 2 - up), o + ImVec2(pad - 3 * S + lw, (h + lh) / 2 - up));
    ic = lw;
    dl->AddText(fBold, 20 * S, o + ImVec2(pad + ic + 10 * S, h / 2 - 13 * S - up), C_TEXT, "LAUNCHER");

    // right side: status, launch button, language, settings
    ImGuiStyle& st = ImGui::GetStyle();
    float y = (h - ImGui::GetFrameHeight()) / 2;
    const char* langs[3] = {"RU", "EN", "ES"};
    float langW = 46 * S;
    std::string setLbl = T(T_SETTINGS);
    if (g_host.overlay) {
        int m, k; { std::lock_guard<std::mutex> lk(g_setMu); m = g_set.overlayMods; k = g_set.overlayVk; }
        setLbl = std::string(T(T_CLOSE)) + "  (" + HotkeyName(m, k) + ")";
    }
    float setW = ImGui::CalcTextSize(setLbl.c_str()).x + st.FramePadding.x * 2;
    float x = width - pad - setW;
    ImGui::SetCursorPos(ImVec2(x, y));
    if (g_host.overlay) { if (AccentButton(setLbl.c_str(), ImVec2(setW, 0))) g_host.returnToGame(); }
    else if (ImGui::Button(setLbl.c_str())) g_openSettings = true;
    float linkW = ImGui::CalcTextSize(EX_LINKS[L()]).x + st.FramePadding.x * 2;
    x -= 8 * S + linkW;
    ImGui::SetCursorPos(ImVec2(x, y));
    if (ImGui::Button(EX_LINKS[L()])) g_openLinks = true;
    x -= 14 * S + langW * 3 + 4 * S * 2;
    for (int i = 0; i < 3; i++) {
        ImGui::SetCursorPos(ImVec2(x + i * (langW + 4 * S), y));
        bool on = g_set.lang == i;
        if (on) { ImGui::PushStyleColor(ImGuiCol_Button, V(C_ACCENT)); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, V(C_ACCENT_HOV)); }
        if (ImGui::Button(langs[i], ImVec2(langW, 0)) && !on) {
            { std::lock_guard<std::mutex> lk(g_setMu); g_set.lang = i; }
            SettingsSave();
        }
        if (on) ImGui::PopStyleColor(2);
    }
    if (g_host.overlay || g_host.gameState() == GS_NOEXE) return;
    // status
    int gs = g_host.gameState();
    const char* stt = ""; ImU32 dot = C_DIM;
    switch (gs) {
    case GS_NOEXE: stt = T(T_GAME_MISSING); dot = C_RED; break;
    case GS_IDLE: case GS_LAUNCHING: stt = T(T_ST_LAUNCHING); dot = C_YELLOW; break;
    case GS_WAIT_INIT: stt = T(T_ST_WAITING); dot = C_YELLOW; break;
    case GS_READY: stt = T(T_ST_READY); dot = C_GREEN; break;
    case GS_CLOSED: stt = T(T_ST_CLOSED); dot = C_DIM; break;
    case GS_FAILED: stt = T(T_ST_ERROR); dot = C_RED; break;
    }
    bool showLaunch = gs == GS_CLOSED || gs == GS_FAILED;
    float lx = x - 24 * S;
    if (showLaunch) {
        float bw = ImGui::CalcTextSize(T(T_BTN_START)).x + st.FramePadding.x * 2;
        lx -= bw;
        ImGui::SetCursorPos(ImVec2(lx, y));
        if (AccentButton(T(T_BTN_START), ImVec2(bw, 0))) g_host.launchGame();
        lx -= 16 * S;
    }
    ImVec2 ts = fSemi->CalcTextSizeA(15 * S, FLT_MAX, 0, stt);
    float sx = lx - ts.x;
    ImVec2 pill0 = o + ImVec2(sx - 30 * S, h / 2 - ts.y / 2 - 6 * S), pill1 = o + ImVec2(lx + 12 * S, h / 2 + ts.y / 2 + 6 * S);
    dl->AddRectFilled(pill0, pill1, IM_COL32(32, 35, 41, 255), 30 * S);
    float pulse = (gs == GS_LAUNCHING || gs == GS_WAIT_INIT || gs == GS_IDLE) ? 0.55f + 0.45f * (float)std::sin(ImGui::GetTime() * 4) : 1.0f;
    ImVec4 dc = V(dot); dc.w *= pulse;
    dl->AddCircleFilled(o + ImVec2(sx - 15 * S, h / 2), 5 * S, ImGui::ColorConvertFloat4ToU32(dc));
    dl->AddText(fSemi, 15 * S, o + ImVec2(sx, h / 2 - ts.y / 2), C_TEXT, stt);
    if (gs == GS_FAILED) {
        ImGui::SetCursorScreenPos(pill0);
        ImGui::InvisibleButton("st_tip", ImVec2(pill1.x - pill0.x, pill1.y - pill0.y));
        if (ImGui::IsItemHovered() && ImGui::BeginTooltip()) {
            ImGui::PushTextWrapPos(360 * S); ImGui::TextUnformatted(T(T_LAUNCH_FAIL)); ImGui::PopTextWrapPos(); ImGui::EndTooltip();
        }
    }
}


#ifndef LL_OVERLAY
#include "setup.h"
static const char* SU_TITLE[3] = {"Игра не найдена", "Game not found", "No se encontró el juego"};
static const char* SU_SUB[3] = {"Лаунчер должен лежать в папке с игрой. Выберите, что сделать:", "The launcher has to be in the game folder. Choose what to do:", "El lanzador debe estar en la carpeta del juego. Elige qué hacer:"};
static const char* SU_PICK[3] = {"Указать папку с игрой", "Choose the game folder", "Elegir la carpeta del juego"};
static const char* SU_PICK_D[3] = {"Если Loadout уже есть на компьютере", "If Loadout is already on this computer", "Si Loadout ya está en este equipo"};
static const char* SU_DL[3] = {"Скачать игру", "Download the game", "Descargar el juego"};
static const char* SU_DL_D[3] = {"Архив около 2,2 ГБ, после распаковки нужно ещё около 6 ГБ", "About 2.2 GB archive, about 6 GB more after unpacking", "Archivo de unos 2,2 GB, unos 6 GB más al descomprimir"};
static const char* SU_PICK_T[3] = {"Папка с игрой Loadout", "Loadout game folder", "Carpeta del juego Loadout"};
static const char* SU_DEST_T[3] = {"Куда установить игру", "Where to install the game", "Dónde instalar el juego"};
static const char* SU_NOTFOUND[3] = {"В этой папке нет Loadout.exe", "There is no Loadout.exe in this folder", "No hay Loadout.exe en esta carpeta"};
static const char* SU_DLING[3] = {"Скачивание игры", "Downloading the game", "Descargando el juego"};
static const char* SU_UNPACK[3] = {"Распаковка", "Unpacking", "Descomprimiendo"};
static const char* SU_CANCEL[3] = {"Отмена", "Cancel", "Cancelar"};
static const char* SU_DONE[3] = {"Игра готова", "The game is ready", "El juego está listo"};
static const char* SU_SHORTCUT[3] = {"Создать ярлык лаунчера на рабочем столе", "Create a launcher shortcut on the desktop", "Crear un acceso directo en el escritorio"};
static const char* SU_FINISH[3] = {"Готово — перенести лаунчер в папку игры", "Done — move the launcher into the game folder", "Listo: mover el lanzador a la carpeta del juego"};
static const char* SU_FINISH_SAME[3] = {"Готово", "Done", "Listo"};
static const char* SU_MOVE_FAIL[3] = {"Не удалось перенести лаунчер в папку игры. Скопируйте его туда вручную.", "Could not move the launcher into the game folder. Copy it there manually.", "No se pudo mover el lanzador. Cópialo manualmente."};
static const char* SU_RETRY[3] = {"Повторить", "Retry", "Reintentar"};
static const char* SU_BACK[3] = {"Назад", "Back", "Atrás"};
static const char* SU_ERR[8][3] = {
    {"Ошибка", "Error", "Error"},
    {"Не удалось получить ссылку на скачивание. Проверьте интернет.", "Could not get the download link. Check the internet connection.", "No se pudo obtener el enlace. Revisa la conexión."},
    {"Связь оборвалась. Нажмите «Повторить» — скачивание продолжится с того же места.", "The connection dropped. Press Retry — the download continues where it stopped.", "Se perdió la conexión. Pulsa Reintentar: la descarga continuará."},
    {"Не хватает места на диске. Нужно свободных ГБ: ", "Not enough disk space. Free GB needed: ", "No hay espacio suficiente. GB libres necesarios: "},
    {"Не удалось записать файл. Проверьте место на диске и права на папку.", "Could not write a file. Check free space and folder permissions.", "No se pudo escribir un archivo. Revisa el espacio y los permisos."},
    {"Архив повреждён. Нажмите «Повторить».", "The archive is damaged. Press Retry.", "El archivo está dañado. Pulsa Reintentar."},
    {"В архиве не нашлось Loadout.exe.", "Loadout.exe was not found in the archive.", "No se encontró Loadout.exe en el archivo."},
    {"Отменено", "Cancelled", "Cancelado"},
};
static std::wstring s_pickedGame;
static std::string s_setupMsg;
static bool s_shortcut = true;

static std::string GB(uint64_t b) { char t[32]; snprintf(t, sizeof(t), "%.2f", b / 1073741824.0); return t; }

static void ProgressBar(float frac, ImVec2 size)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p, p + size, C_CARD, size.y / 2);
    if (frac > 0) dl->AddRectFilled(p, p + ImVec2(std::max(size.y, size.x * frac), size.y), C_ACCENT, size.y / 2);
    ImGui::Dummy(size);
}

static void DrawSetup(float W, float H, float top)
{
    SetupStatus st = SetupGet();
    float cw = std::min(W - 80 * S, 900 * S);
    float x0 = (W - cw) / 2;
    ImGui::SetCursorPos(ImVec2(x0, top + 50 * S));
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cw);
    bool done = st.phase == ST_DONE || !s_pickedGame.empty();
    std::wstring gameDir = !s_pickedGame.empty() ? s_pickedGame : st.gameDir;

    if (done) {
        ImGui::PushFont(fBold, 28 * S); ImGui::TextUnformatted(SU_DONE[L()]); ImGui::PopFont();
        ImGui::TextColored(V(C_DIM), "%s", W2U(gameDir).c_str());
        ImGui::Dummy(ImVec2(0, 12 * S));
        ImGui::Checkbox(SU_SHORTCUT[L()], &s_shortcut);
        ImGui::Dummy(ImVec2(0, 12 * S));
        bool same = _wcsicmp(g_host.gameFolder.c_str(), gameDir.c_str()) == 0;
        ImGui::PushFont(fBold, 19 * S);
        if (AccentButton(same ? SU_FINISH_SAME[L()] : SU_FINISH[L()], ImVec2(cw * 0.6f, 54 * S))) {
            if (SetupMoveLauncher(gameDir, s_shortcut)) {
                if (same) g_host.launchGame(); else PostQuitMessage(0);
            } else s_setupMsg = SU_MOVE_FAIL[L()];
        }
        ImGui::PopFont();
        if (!s_setupMsg.empty()) ImGui::TextColored(V(C_RED), "%s", s_setupMsg.c_str());
    } else if (st.phase == ST_LINK || st.phase == ST_DOWNLOADING || st.phase == ST_EXTRACTING) {
        bool dl = st.phase != ST_EXTRACTING;
        ImGui::PushFont(fBold, 28 * S); ImGui::TextUnformatted(dl ? SU_DLING[L()] : SU_UNPACK[L()]); ImGui::PopFont();
        ImGui::TextColored(V(C_DIM), "%s", W2U(st.destDir).c_str());
        ImGui::Dummy(ImVec2(0, 14 * S));
        float frac = st.total ? (float)((double)st.got / (double)st.total) : 0.0f;
        ProgressBar(st.phase == ST_LINK ? 0.0f : frac, ImVec2(cw, 14 * S));
        ImGui::Dummy(ImVec2(0, 6 * S));
        if (st.phase != ST_LINK) {
            std::string line = GB(st.got) + " / " + GB(st.total) + (L() == 1 ? " GB" : " ГБ");
            if (L() == 2) line = GB(st.got) + " / " + GB(st.total) + " GB";
            line += "   " + std::to_string((int)(frac * 100)) + "%";
            if (dl && st.speed > 0) {
                char t[64]; snprintf(t, sizeof(t), "   %.1f %s", st.speed / 1048576.0, L() == 0 ? "МБ/с" : "MB/s");
                line += t;
                double left = (double)(st.total - st.got) / st.speed;
                snprintf(t, sizeof(t), "   ~%d %s", (int)(left / 60) + 1, L() == 0 ? "мин" : "min");
                line += t;
            }
            ImGui::TextUnformatted(line.c_str());
        }
        ImGui::Dummy(ImVec2(0, 10 * S));
        if (ImGui::Button(SU_CANCEL[L()], ImVec2(160 * S, 0))) SetupCancel();
    } else {
        ImGui::PushFont(fBold, 30 * S); ImGui::TextUnformatted(SU_TITLE[L()]); ImGui::PopFont();
        ImGui::TextColored(V(C_DIM), "%s", SU_SUB[L()]);
        ImGui::Dummy(ImVec2(0, 16 * S));
        float gap = 20 * S, bw = (cw - gap) / 2, bh = 120 * S;
        if (TextCard("pick", bw, bh, SU_PICK[L()], SU_PICK_D[L()], nullptr, 0, false, false)) {
            std::wstring d = SetupPickFolder(GetActiveWindow(), U2W(SU_PICK_T[L()]).c_str());
            if (!d.empty()) {
                std::wstring g = SetupFindGame(d);
                if (g.empty()) s_setupMsg = SU_NOTFOUND[L()]; else { s_pickedGame = g; s_setupMsg.clear(); }
            }
        }
        ImGui::SameLine(0, gap);
        if (TextCard("dl", bw, bh, SU_DL[L()], SU_DL_D[L()], nullptr, 0, false, false)) {
            std::wstring d = SetupPickFolder(GetActiveWindow(), U2W(SU_DEST_T[L()]).c_str());
            if (!d.empty()) { s_setupMsg.clear(); SetupStartDownload(d); }
        }
        if (st.phase == ST_ERROR) {
            ImGui::Dummy(ImVec2(0, 10 * S));
            int c = st.errorCode >= 1 && st.errorCode <= 7 ? st.errorCode : 0;
            std::string m = SU_ERR[c][L()];
            if (c == 3) m += st.error;
            ImGui::TextColored(V(c == 7 ? C_DIM : C_RED), "%s", m.c_str());
            if (c != 7 && !st.destDir.empty()) {
                if (AccentButton(SU_RETRY[L()], ImVec2(180 * S, 0))) SetupStartDownload(st.destDir);
            }
        }
        if (!s_setupMsg.empty()) { ImGui::Dummy(ImVec2(0, 10 * S)); ImGui::TextColored(V(C_RED), "%s", s_setupMsg.c_str()); }
    }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
}
#endif

static void DrawUIInner(ImVec2 pos, ImVec2 size)
{
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    if (g_host.overlay) { ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12 * S); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f); }
    ImGui::Begin("main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (g_host.overlay) ImGui::PopStyleVar(2);
    float W = size.x, H = size.y;
    float headH = 72 * S;
    DrawHeader(W, headH);

    if (g_host.gameState() == GS_NOEXE) {
#ifndef LL_OVERLAY
        DrawSetup(W, H, headH);
#endif
        DrawPopups();
        ImGui::End();
        return;
    }

    float rightW = std::floor(390 * S);
    if (rightW > W * 0.45f) rightW = std::floor(W * 0.45f);
    float leftW = W - rightW;

    // left: steps + content
    ImGui::SetCursorPos(ImVec2(0, headH));
    ImGui::BeginChild("left", ImVec2(leftW, H - headH), 0, ImGuiWindowFlags_NoScrollbar);
    {
        float pad = 24 * S;
        ImGui::SetCursorPos(ImVec2(pad, 16 * S));
        ImGui::BeginGroup();
        if (step > 0) {
            if (ImGui::Button((std::string("< ") + T(T_BACK)).c_str())) {
                if (step == 2 && selBase >= 0) step = 1; else step = 0;
            }
            ImGui::SameLine(0, 16 * S);
        }
        DrawSteps();
        ImGui::EndGroup();
        ImGui::SetCursorPosX(pad);
        ImGui::PushFont(fBold, 24 * S);
        const char* head = step == 0 ? T(T_CHOOSE_MAP) : step == 1 ? T(T_CHOOSE_TIME) : T(T_CHOOSE_MODE);
        ImGui::TextUnformatted(head);
        ImGui::PopFont();

        ImGui::SetCursorPosX(pad);
        ImGui::BeginChild("content", ImVec2(leftW - pad, 0), 0, 0);
        ImGui::PushClipRect(ImGui::GetWindowPos(), ImGui::GetWindowPos() + ImGui::GetWindowSize(), false);
        ImGui::SetCursorPos(ImVec2(0, 6 * S));
        ImGui::BeginGroup();
        ImGui::PushItemWidth(-1);
        float innerW = leftW - pad * 2 - ImGui::GetStyle().ScrollbarSize;
        ImGui::BeginChild("inner", ImVec2(innerW, 0), ImGuiChildFlags_AutoResizeY, 0);
        if (step == 0 || selBase < 0) DrawMapGrid();
        else if (step == 1) DrawTimeStep();
        else DrawModeStep();
        ImGui::Dummy(ImVec2(0, 10 * S));
        ImGui::EndChild();
        ImGui::PopItemWidth();
        ImGui::EndGroup();
        ImGui::PopClipRect();
        ImGui::EndChild();
    }
    ImGui::EndChild();

    // right panel
    ImGui::SetCursorPos(ImVec2(leftW, headH));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, V(C_PANEL));
    ImGui::BeginChild("right", ImVec2(rightW, H - headH), 0, 0);
    ImGui::PopStyleColor();
    {
        ImVec2 o = ImGui::GetWindowPos();
        ImGui::GetWindowDrawList()->AddLine(o, o + ImVec2(0, H), C_LINE, 1);
        DrawRightPanel(rightW - ImGui::GetStyle().ScrollbarSize * 0.5f);
    }
    ImGui::EndChild();

    DrawPopups();
    ImGui::End();
}



// ------------------------------------------------------------------ public
void UiInit(ID3D11Device* dev, ID3D11DeviceContext* ctx, HMODULE resModule, float scale)
{
    for (auto& t : g_tex) if (t.second.id) ((ID3D11ShaderResourceView*)(intptr_t)t.second.id)->Release();
    g_tex.clear();
    s_dev = dev; s_ctx = ctx; s_mod = resModule; S = scale;
    LoadFonts();
    ApplyStyle();
    LoadTextures();
    std::string c; { std::lock_guard<std::mutex> lk(g_setMu); c = g_set.customMap; }
    strncpy(customBuf, c.c_str(), sizeof(customBuf) - 1);
}

void UiSetScale(float scale) { S = scale; ApplyStyle(); }

void UiHotkeyCaptured(int mods, int vk)
{
    g_uiCaptureHotkey = false;
    if (vk == VK_ESCAPE) return;
    { std::lock_guard<std::mutex> lk(g_setMu); g_set.overlayMods = mods; g_set.overlayVk = vk; }
    SettingsSave();
}

void UiOnOpen()
{
    // pick up what the game has right now
    std::string now = g_host.currentMap();
    if (finalCode.empty() && !now.empty()) {
        bool night; std::string suf;
        int b = ParseCode(now, &night, &suf);
        if (b >= 0) { selBase = b; selNight = night ? 1 : 0; selMode = suf; selModeChosen = true; step = BASE_MAPS[b].single ? 0 : 2; finalCode = now; }
    }
}

void UiDraw()
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    if (!g_host.overlay) { DrawUIInner(vp->WorkPos, vp->WorkSize); return; }
    // in-game: dim the game and show the launcher in the middle
    ImGui::GetBackgroundDrawList()->AddRectFilled(vp->Pos, vp->Pos + vp->Size, IM_COL32(0, 0, 0, 150));
    ImVec2 size(std::floor(vp->Size.x * 0.9f), std::floor(vp->Size.y * 0.88f));
    if (size.x > 1700 * S) size.x = 1700 * S;
    ImVec2 pos(vp->Pos.x + (vp->Size.x - size.x) / 2, vp->Pos.y + (vp->Size.y - size.y) / 2);
    DrawUIInner(pos, size);
}
