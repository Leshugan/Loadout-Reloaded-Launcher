#include "setup.h"
#include "settings.h"
#include "game.h"
#include "ui.h"
#include <winhttp.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <cstdio>
#include "miniz.h"

static const char* GAME_PAGE = "https://www.mediafire.com/file/5wq0ooues6haxvh/Loadout.zip/file";

static std::mutex s_mu;
static SetupStatus s_game, s_ar;          // first-run install / archive-only download (links)
static std::atomic<bool> s_arMode{false};  // which one the download code updates
static SetupStatus& St() { return s_arMode ? s_ar : s_game; }
#define s_st St()
static std::atomic<bool> s_cancel{false};
static std::atomic<bool> s_busy{false};

SetupStatus SetupGet() { std::lock_guard<std::mutex> lk(s_mu); return s_game; }
SetupStatus SetupArchiveGet() { std::lock_guard<std::mutex> lk(s_mu); return s_ar; }
static void SetPhase(int ph) { std::lock_guard<std::mutex> lk(s_mu); s_st.phase = ph; }
static void Fail(int code, const std::string& msg) { std::lock_guard<std::mutex> lk(s_mu); s_st.phase = ST_ERROR; s_st.errorCode = code; s_st.error = msg; LogF("setup error %d: %s", code, msg.c_str()); }
void SetupCancel() { s_cancel = true; }

// ------------------------------------------------------------------ helpers
static bool FileExists(const std::wstring& p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }

static std::wstring FindGameRec(const std::wstring& dir, int depth)
{
    if (FileExists(dir + L"\\Loadout.exe")) return dir;
    if (depth <= 0) return {};
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::wstring found;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        found = FindGameRec(dir + L"\\" + fd.cFileName, depth - 1);
    } while (found.empty() && FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}
std::wstring SetupFindGame(const std::wstring& dir) { return dir.empty() ? std::wstring() : FindGameRec(dir, 3); }

std::wstring SetupPickFolder(HWND owner, const wchar_t* title, const std::wstring& startDir)
{
    std::wstring res;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return res;
    DWORD opt = 0; dlg->GetOptions(&opt);
    dlg->SetOptions(opt | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(title);
    if (!startDir.empty()) {
        IShellItem* si = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(startDir.c_str(), nullptr, IID_PPV_ARGS(&si)))) { dlg->SetFolder(si); si->Release(); }
    }
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* it = nullptr;
        if (SUCCEEDED(dlg->GetResult(&it))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) { res = p; CoTaskMemFree(p); }
            it->Release();
        }
    }
    dlg->Release();
    while (res.size() > 3 && res.back() == L'\\') res.pop_back();
    return res;
}

// ------------------------------------------------------------------ download
static bool Download(const std::string& urlUtf8, const std::wstring& file)
{
    std::wstring url = U2W(urlUtf8);
    URL_COMPONENTS uc{}; uc.dwStructSize = sizeof(uc);
    wchar_t host[256], path[4096];
    uc.lpszHostName = host; uc.dwHostNameLength = 256; uc.lpszUrlPath = path; uc.dwUrlPathLength = 4096;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) { Fail(1, "bad link"); return false; }

    uint64_t have = 0;
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &fa)) have = ((uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;

    HINTERNET ses = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36",
                                WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) ses = WinHttpOpen(L"Mozilla/5.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET con = ses ? WinHttpConnect(ses, host, uc.nPort, 0) : nullptr;
    HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    bool ok = false;
    if (req) {
        std::wstring hdr;
        if (have) hdr = L"Range: bytes=" + std::to_wstring(have) + L"-\r\n";
        if (WinHttpSendRequest(req, hdr.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdr.c_str(), (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(req, nullptr)) {
            DWORD status = 0, sz = sizeof(status);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
            wchar_t lenBuf[64] = L""; DWORD lb = sizeof(lenBuf);
            uint64_t len = 0;
            if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lenBuf, &lb, WINHTTP_NO_HEADER_INDEX)) len = _wcstoui64(lenBuf, nullptr, 10);
            if (status == 200) have = 0;            // server ignored the range: start over
            LogF("download: http %lu, length %llu, resume from %llu", status, (unsigned long long)len, (unsigned long long)have);
            if (status == 200 || status == 206) {
                FILE* f = _wfopen(file.c_str(), have ? L"ab" : L"wb");
                if (!f) Fail(4, "cannot write the archive");
                else {
                    { std::lock_guard<std::mutex> lk(s_mu); s_st.total = have + len; s_st.got = have; s_st.phase = ST_DOWNLOADING; }
                    std::vector<char> buf(1 << 20);
                    ULONGLONG t0 = GetTickCount64(); uint64_t got0 = have, got = have;
                    ok = true;
                    for (;;) {
                        if (s_cancel) { ok = false; Fail(7, "cancelled"); break; }
                        DWORD avail = 0;
                        if (!WinHttpQueryDataAvailable(req, &avail)) { ok = false; Fail(2, "connection lost"); break; }
                        if (!avail) break;
                        if (avail > buf.size()) avail = (DWORD)buf.size();
                        DWORD rd = 0;
                        if (!WinHttpReadData(req, buf.data(), avail, &rd)) { ok = false; Fail(2, "connection lost"); break; }
                        if (!rd) break;
                        if (fwrite(buf.data(), 1, rd, f) != rd) { ok = false; Fail(4, "cannot write the archive (disk full?)"); break; }
                        got += rd;
                        ULONGLONG now = GetTickCount64();
                        std::lock_guard<std::mutex> lk(s_mu);
                        s_st.got = got;
                        if (now - t0 >= 1000) { s_st.speed = (double)(got - got0) * 1000.0 / (double)(now - t0); t0 = now; got0 = got; }
                    }
                    fclose(f);
                    if (ok && len && got < have + len) { ok = false; Fail(2, "download was interrupted"); }
                }
            } else Fail(2, "server answered " + std::to_string(status));
        } else Fail(2, "no connection");
    } else Fail(2, "no connection");
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    if (ses) WinHttpCloseHandle(ses);
    return ok;
}

// ------------------------------------------------------------------ unpack
struct WriteCtx { FILE* f; uint64_t* done; };
static size_t WriteCb(void* opaque, mz_uint64, const void* buf, size_t n)
{
    WriteCtx* c = (WriteCtx*)opaque;
    if (s_cancel) return 0;
    size_t w = fwrite(buf, 1, n, c->f);
    *c->done += w;
    std::lock_guard<std::mutex> lk(s_mu);
    s_st.got = *c->done;
    return w;
}

static void MakeDirsW(const std::wstring& path)
{
    for (size_t i = 3; i < path.size(); i++) if (path[i] == L'\\') CreateDirectoryW(path.substr(0, i).c_str(), nullptr);
    CreateDirectoryW(path.c_str(), nullptr);
}

static bool Unpack(const std::wstring& zipPath, const std::wstring& dest)
{
    mz_zip_archive za{};
    if (!mz_zip_reader_init_file(&za, W2U(zipPath).c_str(), 0)) { Fail(5, "the archive is damaged"); return false; }
    mz_uint n = mz_zip_reader_get_num_files(&za);
    uint64_t total = 0;
    for (mz_uint i = 0; i < n; i++) { mz_zip_archive_file_stat st; if (mz_zip_reader_file_stat(&za, i, &st)) total += st.m_uncomp_size; }
    ULARGE_INTEGER freeB{};
    if (GetDiskFreeSpaceExW(dest.c_str(), &freeB, nullptr, nullptr) && freeB.QuadPart < total + (64ull << 20)) {
        mz_zip_reader_end(&za);
        Fail(3, std::to_string((total >> 30) + 1));
        return false;
    }
    { std::lock_guard<std::mutex> lk(s_mu); s_st.phase = ST_EXTRACTING; s_st.total = total; s_st.got = 0; s_st.speed = 0; }
    uint64_t done = 0;
    bool ok = true;
    for (mz_uint i = 0; i < n && ok; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&za, i, &st)) { ok = false; Fail(5, "the archive is damaged"); break; }
        std::wstring rel = U2W(st.m_filename);
        for (auto& ch : rel) if (ch == L'/') ch = L'\\';
        if (rel.find(L"..") != std::wstring::npos) continue;
        std::wstring out = dest + L"\\" + rel;
        if (st.m_is_directory) { MakeDirsW(out); continue; }
        MakeDirsW(out.substr(0, out.rfind(L'\\')));
        FILE* f = _wfopen(out.c_str(), L"wb");
        if (!f) { ok = false; Fail(4, "cannot write " + W2U(rel)); break; }
        WriteCtx c{f, &done};
        mz_bool r = mz_zip_reader_extract_to_callback(&za, i, WriteCb, &c, 0);
        fclose(f);
        if (!r) { ok = false; if (s_cancel) Fail(7, "cancelled"); else Fail(5, "cannot unpack " + W2U(rel)); }
    }
    mz_zip_reader_end(&za);
    return ok;
}

void SetupStartDownload(const std::wstring& destDir)
{
    if (s_busy.exchange(true)) return;
    s_cancel = false;
    s_arMode = false;
    { std::lock_guard<std::mutex> lk(s_mu); s_game = SetupStatus(); s_game.phase = ST_LINK; s_game.destDir = destDir; }
    std::thread([destDir]() {
        struct Done { ~Done() { s_busy = false; } } done;
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        // free space for archive + unpacked game (about 8 GB)
        ULARGE_INTEGER freeB{};
        if (GetDiskFreeSpaceExW(destDir.c_str(), &freeB, nullptr, nullptr) && freeB.QuadPart < (8ull << 30)) { Fail(3, "8"); return; }
        std::wstring zip = destDir + L"\\Loadout.zip.part";
        bool ok = false;
        for (int attempt = 0; attempt < 5 && !ok && !s_cancel; attempt++) {
            SetPhase(ST_LINK);
            std::string link = FetchMediafireDirect(GAME_PAGE);   // a fresh link every time, like pressing the button
            LogF("download link: %s", link.empty() ? "(none)" : "ok");
            if (link.empty()) { Fail(1, "no link"); Sleep(2000); continue; }
            ok = Download(link, zip);
            if (!ok && !s_cancel) Sleep(2000);
        }
        if (!ok) { if (s_cancel) DeleteFileW(zip.c_str()); return; }   // cancelled: don't leave the half-downloaded archive
        if (!Unpack(zip, destDir)) { if (s_cancel) DeleteFileW(zip.c_str()); return; }
        DeleteFileW(zip.c_str());
        std::wstring game = SetupFindGame(destDir);
        if (game.empty()) { Fail(6, "Loadout.exe not found in the archive"); return; }
        std::lock_guard<std::mutex> lk(s_mu);
        s_st.gameDir = game;
        s_st.phase = ST_DONE;
        LogF("game installed to %s", W2U(game).c_str());
    }).detach();
}

std::wstring SetupDownloadsDir()
{
    PWSTR dl = nullptr; std::wstring d;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &dl))) { d = dl; CoTaskMemFree(dl); }
    return d;
}

// Links → "Download the game": just the archive, into the folder the user picked.
void SetupStartArchive(const std::wstring& dir)
{
    if (s_busy.exchange(true)) return;
    s_cancel = false;
    s_arMode = true;
    { std::lock_guard<std::mutex> lk(s_mu); s_ar = SetupStatus(); s_ar.phase = ST_LINK; }
    std::thread([dir]() {
        struct Done { ~Done() { s_busy = false; } } done;
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        std::wstring part = dir + L"\\Loadout.zip.part", zip = dir + L"\\Loadout.zip";
        bool ok = false;
        for (int attempt = 0; attempt < 5 && !ok && !s_cancel; attempt++) {
            SetPhase(ST_LINK);
            std::string link = FetchMediafireDirect(GAME_PAGE);
            if (link.empty()) { Sleep(2000); continue; }
            ok = Download(link, part);
            if (!ok && !s_cancel) Sleep(2000);
        }
        if (!ok) { if (s_cancel) DeleteFileW(part.c_str()); if (!s_cancel) Fail(2, "download failed"); return; }
        MoveFileExW(part.c_str(), zip.c_str(), MOVEFILE_REPLACE_EXISTING);
        { std::lock_guard<std::mutex> lk(s_mu); s_ar.phase = ST_DONE; s_ar.gameDir = zip; }
        LogF("archive saved: %s", W2U(zip).c_str());
        std::wstring args = L"/select,\"" + zip + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    }).detach();
}

// ------------------------------------------------------------------ move launcher into the game folder
static bool CreateDesktopShortcut(const std::wstring& target, const std::wstring& workDir)
{
    PWSTR desk = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desk))) return false;
    std::wstring lnk = std::wstring(desk) + L"\\Loadout.lnk";
    CoTaskMemFree(desk);
    IShellLinkW* sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl)))) return false;
    sl->SetPath(target.c_str());
    sl->SetWorkingDirectory(workDir.c_str());
    sl->SetIconLocation(target.c_str(), 0);
    sl->SetDescription(L"Loadout Launcher");
    IPersistFile* pf = nullptr;
    bool ok = false;
    if (SUCCEEDED(sl->QueryInterface(IID_PPV_ARGS(&pf)))) { ok = SUCCEEDED(pf->Save(lnk.c_str(), TRUE)); pf->Release(); }
    sl->Release();
    return ok;
}

bool SetupMoveLauncher(const std::wstring& gameDir, bool shortcut)
{
    wchar_t self[MAX_PATH]; GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring target = gameDir + L"\\LoadoutLauncher.exe";
    bool same = _wcsicmp(self, target.c_str()) == 0;
    if (!same && !CopyFileW(self, target.c_str(), FALSE)) { LogF("cannot copy launcher to %s (err %lu)", W2U(target).c_str(), GetLastError()); return false; }
    // keep settings made before the move
    std::wstring oldIni = g_iniPath;     // current settings file
    if (!same && FileExists(oldIni)) {
        CreateDirectoryW((gameDir + L"\\LoadoutLauncher_Data").c_str(), nullptr);
        CopyFileW(oldIni.c_str(), (gameDir + L"\\LoadoutLauncher_Data\\LoadoutLauncher.ini").c_str(), TRUE);
    }
    if (shortcut) LogF("desktop shortcut: %s", CreateDesktopShortcut(target, gameDir) ? "ok" : "FAILED");
    if (same) return true;
    std::wstring cmd = L"\"" + target + L"\" --cleanup \"" + self + L"\"";
    STARTUPINFOW si{}; si.cb = sizeof(si); PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    if (!CreateProcessW(target.c_str(), buf.data(), nullptr, nullptr, FALSE, 0, nullptr, gameDir.c_str(), &si, &pi)) { LogF("cannot start the moved launcher (err %lu)", GetLastError()); return false; }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    LogF("launcher moved to %s", W2U(target).c_str());
    return true;
}

void SetupCleanupOld(const std::wstring& oldExe)
{
    std::thread([oldExe]() {
        std::wstring dir = oldExe.substr(0, oldExe.rfind(L'\\'));
        for (int i = 0; i < 60; i++) {
            if (DeleteFileW(oldExe.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) break;
            Sleep(500);
        }
        DeleteFileW((dir + L"\\LoadoutLauncher.ini").c_str());
        std::wstring data = dir + L"\\LoadoutLauncher_Data";   // the old copy's logs, settings and emulator files
        if (_wcsicmp(data.c_str(), AppDataDir().c_str()) != 0 && GetFileAttributesW(data.c_str()) != INVALID_FILE_ATTRIBUTES) {
            std::wstring from = data; from.push_back(0);
            SHFILEOPSTRUCTW op{}; op.wFunc = FO_DELETE; op.pFrom = from.c_str(); op.fFlags = FOF_NO_UI;
            SHFileOperationW(&op);
        }
        LogF("old launcher removed: %s", W2U(oldExe).c_str());
    }).detach();
}
