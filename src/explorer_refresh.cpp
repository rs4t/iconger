#include "explorer_refresh.h"
#include <shlobj.h>
#include <tlhelp32.h>
#include <cstdio>
#include <vector>
#include <filesystem>
#include <string>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

// ----------------------------------------------------------------------
// Lightweight refresh
// ----------------------------------------------------------------------

bool SignalIconChange() {
    // SHCNE_ASSOCCHANGED tells the shell to re-read icon associations.
    // SHCNE_UPDATEIMAGE tells it to refresh the icon cache for a specific image.
    // We send both as a best-effort approach.
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    SHChangeNotify(SHCNE_UPDATEIMAGE, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

// ----------------------------------------------------------------------
// Icon cache clearing
// ----------------------------------------------------------------------

bool ClearIconCache() {
    wchar_t explorerPath[MAX_PATH] = {};
    HRESULT hr = SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr,
                                   SHGFP_TYPE_CURRENT, explorerPath);
    if (FAILED(hr)) {
        // Try environment variable fallback
        DWORD ret = GetEnvironmentVariableW(L"LOCALAPPDATA", explorerPath, MAX_PATH);
        if (ret == 0 || ret >= MAX_PATH) return false;
    }

    std::wstring cacheDir = std::wstring(explorerPath) +
        L"\\Microsoft\\Windows\\Explorer";

    if (!std::filesystem::exists(cacheDir))
        return false;

    bool anyDeleted = false;

    // Delete iconcache_*.db files
    try {
        for (auto& entry : std::filesystem::directory_iterator(cacheDir)) {
            std::wstring filename = entry.path().filename().wstring();
            // Match: iconcache_*.db or IconCache.db
            if (filename.find(L"iconcache_") == 0 &&
                filename.rfind(L".db") == filename.length() - 3) {
                // Try to delete; may fail if explorer is currently reading it
                std::error_code ec;
                std::filesystem::remove(entry.path(), ec);
                if (!ec) anyDeleted = true;
            }
        }

        // Also try IconCache.db (older Windows)
        std::wstring legacyCache = cacheDir + L"\\IconCache.db";
        if (std::filesystem::exists(legacyCache)) {
            std::error_code ec;
            std::filesystem::remove(legacyCache, ec);
            if (!ec) anyDeleted = true;
        }
    } catch (...) {
        // Ignore iteration errors
    }

    return anyDeleted;
}

// ----------------------------------------------------------------------
// Explorer restart
// ----------------------------------------------------------------------

bool RestartExplorer() {
    // First, try to gracefully restart via shell32
    // Use ExitWindowsEx with EWX_RESTART... no, that restarts the whole system.
    // We'll kill explorer.exe — Windows will auto-restart it.

    // Find explorer.exe
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
        return false;

    std::vector<DWORD> explorerPids;
    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(hSnapshot, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"explorer.exe") == 0) {
                explorerPids.push_back(pe.th32ProcessID);
            }
        } while (Process32NextW(hSnapshot, &pe));
    }
    CloseHandle(hSnapshot);

    if (explorerPids.empty()) {
        // Explorer not running — just launch it
        ShellExecuteW(nullptr, L"open", L"explorer.exe", nullptr, nullptr, SW_SHOW);
        return true;
    }

    // Set a flag that tells explorer to restart after shutdown
    // (it checks this via SHGetRestart)
    // Then terminate each explorer process.
    for (DWORD pid : explorerPids) {
        HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (hProc) {
            TerminateProcess(hProc, 0);
            CloseHandle(hProc);
        }
    }

    // Wait a moment then launch explorer if it didn't auto-restart
    Sleep(1000);

    // Check if explorer is running; if not, launch it
    hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    bool found = false;
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnapshot, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"explorer.exe") == 0) {
                    found = true;
                    break;
                }
            } while (Process32NextW(hSnapshot, &pe));
        }
        CloseHandle(hSnapshot);
    }

    if (!found) {
        ShellExecuteW(nullptr, L"open", L"explorer.exe", nullptr, nullptr, SW_SHOW);
    }

    return true;
}

// ----------------------------------------------------------------------
// Nuclear refresh
// ----------------------------------------------------------------------

bool NuclearRefresh() {
    ClearIconCache();
    return RestartExplorer();
}

bool ConfirmNuclearRefresh(HWND parentHwnd) {
    int ret = MessageBoxW(parentHwnd,
        L"Applying icons requires clearing the icon cache and restarting Explorer.\n\n"
        L"This will briefly flicker your desktop and taskbar.\n\n"
        L"Proceed?",
        L"Iconger - Refresh Explorer?",
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    return (ret == IDYES);
}
