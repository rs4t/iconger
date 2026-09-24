#include "explorer_refresh.h"
#include "app_paths.h"
#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <restartmanager.h>
#include <vector>

void SignalIconChange()
{
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSH, nullptr, nullptr);
}

static std::vector<DWORD> FindExplorerPids()
{
    std::vector<DWORD> pids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return pids;
    PROCESSENTRY32W pe = { sizeof(pe) };
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"explorer.exe") == 0) pids.push_back(pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pids;
}

// Explorer only holds the cache files open while it runs, so this has to happen
// while it is stopped. (The old code deleted them first, while they were locked.)
static int DeleteIconCache()
{
    std::wstring dir = ExpandEnv(L"%LOCALAPPDATA%\\Microsoft\\Windows\\Explorer");
    int deleted = 0;
    WIN32_FIND_DATAW ffd = {};
    HANDLE h = FindFirstFileW((dir + L"\\iconcache*.db").c_str(), &ffd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (DeleteFileW((dir + L"\\" + ffd.cFileName).c_str())) ++deleted;
        } while (FindNextFileW(h, &ffd));
        FindClose(h);
    }
    // pre-Windows 8 location
    if (DeleteFileW(ExpandEnv(L"%LOCALAPPDATA%\\IconCache.db").c_str())) ++deleted;
    return deleted;
}

static bool WaitForExplorer(DWORD timeoutMs)
{
    for (DWORD waited = 0; waited < timeoutMs; waited += 250) {
        if (FindWindowW(L"Shell_TrayWnd", nullptr)) return true;
        Sleep(250);
    }
    return FindWindowW(L"Shell_TrayWnd", nullptr) != nullptr;
}

static void LaunchExplorer()
{
    wchar_t path[MAX_PATH] = {};
    GetWindowsDirectoryW(path, MAX_PATH);
    wcscat_s(path, L"\\explorer.exe");
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(path, nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

ExplorerRestartResult RestartExplorer(bool clearIconCache)
{
    ExplorerRestartResult result;

    std::vector<RM_UNIQUE_PROCESS> procs;
    for (DWORD pid : FindExplorerPids()) {
        HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!hp) continue;
        FILETIME created, exited, kernel, user;
        if (GetProcessTimes(hp, &created, &exited, &kernel, &user))
            procs.push_back({ pid, created });
        CloseHandle(hp);
    }

    DWORD session = 0;
    WCHAR key[CCH_RM_SESSION_KEY + 1] = {};
    bool rmSession = RmStartSession(&session, 0, key) == ERROR_SUCCESS;
    bool stopped = procs.empty();
    if (rmSession && !procs.empty() &&
        RmRegisterResources(session, 0, nullptr, (UINT)procs.size(), procs.data(), 0, nullptr) == ERROR_SUCCESS)
        stopped = RmShutdown(session, RmForceShutdown, nullptr) == ERROR_SUCCESS;

    if (!stopped) {
        // Last resort: hard kill. Windows' AutoRestartShell usually brings the shell back.
        result.forced = true;
        for (const auto& p : procs) {
            HANDLE hp = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, p.dwProcessId);
            if (!hp) continue;
            TerminateProcess(hp, 1);
            WaitForSingleObject(hp, 3000);
            CloseHandle(hp);
        }
    }

    if (clearIconCache) result.cacheFilesDeleted = DeleteIconCache();

    if (rmSession) {
        if (stopped && !procs.empty()) RmRestart(session, 0, nullptr);
        RmEndSession(session);
    }

    if (!WaitForExplorer(6000)) {
        // Checking for the tray window (not just the process) avoids starting a
        // second explorer.exe, which would pop open a stray folder window.
        LaunchExplorer();
        WaitForExplorer(6000);
    }
    result.ok = FindWindowW(L"Shell_TrayWnd", nullptr) != nullptr;
    return result;
}
