#pragma once
// First run: find the game folder or download the game, then move the launcher into the game folder.
#include <windows.h>
#include <string>
#include <cstdint>

enum SetupPhase { ST_IDLE = 0, ST_LINK, ST_DOWNLOADING, ST_EXTRACTING, ST_DONE, ST_ERROR };

struct SetupStatus {
    int phase = ST_IDLE;
    uint64_t got = 0, total = 0;     // download bytes, or extracted bytes
    double speed = 0;                // bytes per second
    std::string error;               // text id-free message (already localized by caller code)
    int errorCode = 0;               // 1 no link, 2 network, 3 disk space, 4 write, 5 bad archive, 6 no game in archive, 7 cancelled
    std::wstring gameDir;            // result
    std::wstring destDir;
};

SetupStatus SetupGet();
SetupStatus SetupArchiveGet();                 // links → download archive to Downloads
void SetupStartArchive(const std::wstring& dir);
std::wstring SetupDownloadsDir();
void SetupStartDownload(const std::wstring& destDir);
void SetupCancel();
std::wstring SetupFindGame(const std::wstring& dir);           // folder with Loadout.exe inside dir (depth 3) or empty
std::wstring SetupPickFolder(HWND owner, const wchar_t* title, const std::wstring& startDir = std::wstring());
bool SetupMoveLauncher(const std::wstring& gameDir, bool shortcut); // copy exe, shortcut, start new copy (caller exits)
void SetupCleanupOld(const std::wstring& oldExe);                  // called by the new copy
