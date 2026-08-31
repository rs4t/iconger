#include "taskbar_enum.h"
#include "shell_link.h"
#include <tlhelp32.h>
#include "icon_utils.h"
#include <psapi.h>
#include <shlwapi.h>
#include <oleauto.h>
#include <uiautomation.h>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "oleacc.lib")

// -----------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------

static std::wstring GetExePathFromPid(DWORD pid)
{
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return L"";
    wchar_t path[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    QueryFullProcessImageNameW(hProc, 0, path, &size);
    CloseHandle(hProc);
    return std::wstring(path);
}

// (display name comes from MatchButtonToProcess via button text)

static std::wstring FindMatchingLnk(const std::wstring& exePath)
{
    std::wstring folder = GetPinnedShortcutsFolder();
    std::wstring searchPath = folder + L"\\*.lnk";
    wchar_t exeFname[MAX_PATH] = {};
    _wsplitpath_s(exePath.c_str(), nullptr, 0, nullptr, 0, exeFname, MAX_PATH, nullptr, 0);
    WIN32_FIND_DATAW ffd = {};
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return L"";
    do {
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring fullPath = folder + L"\\" + ffd.cFileName;
        PinnedShortcut sc = ReadShortcut(fullPath);
        if (!sc.valid) continue;
        wchar_t lnkFname[MAX_PATH] = {};
        _wsplitpath_s(sc.targetPath.c_str(), nullptr, 0, nullptr, 0, lnkFname, MAX_PATH, nullptr, 0);
        if (_wcsicmp(exeFname, lnkFname) == 0) {
            if (_wcsicmp(sc.targetPath.c_str(), exePath.c_str()) == 0) {
                FindClose(hFind); return fullPath;
            }
        }
    } while (FindNextFileW(hFind, &ffd) != 0);
    FindClose(hFind);
    hFind = FindFirstFileW(searchPath.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return L"";
    do {
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring fullPath = folder + L"\\" + ffd.cFileName;
        PinnedShortcut sc = ReadShortcut(fullPath);
        if (!sc.valid) continue;
        wchar_t lnkFname[MAX_PATH] = {};
        _wsplitpath_s(sc.targetPath.c_str(), nullptr, 0, nullptr, 0, lnkFname, MAX_PATH, nullptr, 0);
        if (_wcsicmp(exeFname, lnkFname) == 0) {
            FindClose(hFind); return fullPath;
        }
    } while (FindNextFileW(hFind, &ffd) != 0);
    FindClose(hFind);
    return L"";
}

/// Match a taskbar button name (like "Firefox (2) - 1 running window pinned")
/// to a running process, returning the exe path and display name.
/// Returns true if a match was found.
static bool MatchButtonToProcess(const std::wstring& buttonName,
                                   std::wstring& outExePath,
                                   std::wstring& outDisplayName)
{
    if (buttonName.empty()) return false;

    // Common patterns in Win11 taskbar button names:
    //   "AppName pinned"
    //   "AppName - N running windows"
    //   "AppName (N) - M running window pinned"
    //   "AppName"

    // Extract the base app name: take everything before " (", " - ", or " pinned"
    std::wstring appName = buttonName;
    size_t pos = appName.find(L" (");
    if (pos != std::wstring::npos) appName = appName.substr(0, pos);
    pos = appName.find(L" - ");
    if (pos != std::wstring::npos) appName = appName.substr(0, pos);
    pos = appName.find(L" pinned");
    if (pos != std::wstring::npos) appName = appName.substr(0, pos);

    if (appName.empty()) return false;

    outDisplayName = appName;

    // Enumerate running processes and match by executable filename
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);

    // Use a scoring system: match by process name vs app name
    int bestScore = 0;
    std::wstring bestExe;

    if (Process32FirstW(hSnap, &pe)) {
        do {
            // Get the full exe path
            std::wstring exePath = GetExePathFromPid(pe.th32ProcessID);
            if (exePath.empty()) continue;

            // Get the exe filename (without extension)
            wchar_t fname[MAX_PATH] = {};
            _wsplitpath_s(exePath.c_str(), nullptr, 0, nullptr, 0,
                          fname, MAX_PATH, nullptr, 0);

            // Skip system processes
            if (_wcsicmp(fname, L"explorer") == 0 ||
                _wcsicmp(fname, L"svchost") == 0 ||
                _wcsicmp(fname, L"RuntimeBroker") == 0 ||
                _wcsicmp(fname, L"sihost") == 0 ||
                _wcsicmp(fname, L"taskhostw") == 0 ||
                _wcsicmp(fname, L"SearchApp") == 0 ||
                _wcsicmp(fname, L"ApplicationFrameHost") == 0 ||
                _wcsicmp(fname, L"SystemSettings") == 0)
                continue;

            // Score: exact match of base filename (case-insensitive)
            int score = 0;
            std::wstring appLower = appName;
            std::wstring fnameStr = fname;
            for (auto& c : appLower) c = towlower(c);
            for (auto& c : fnameStr) c = towlower(c);

            // Remove .exe if present
            if (fnameStr.size() > 4 && fnameStr.substr(fnameStr.size() - 4) == L".exe")
                fnameStr = fnameStr.substr(0, fnameStr.size() - 4);

            // Check if filename contains the app name or vice versa
            if (fnameStr == appLower) {
                score = 100;
            } else if (fnameStr.find(appLower) != std::wstring::npos) {
                score = 50;
            } else if (appLower.find(fnameStr) != std::wstring::npos) {
                score = 40;
            }

            if (score > bestScore) {
                bestScore = score;
                bestExe = exePath;
            }

        } while (Process32NextW(hSnap, &pe));
    }

    CloseHandle(hSnap);

    if (bestScore >= 40 && !bestExe.empty()) {
        outExePath = bestExe;
        return true;
    }

    return false;
}

// -----------------------------------------------------------------------
// Public API — UI Automation based taskbar enumeration
// -----------------------------------------------------------------------

std::vector<TaskbarButton> EnumerateTaskbarButtons()
{
    std::vector<TaskbarButton> results;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    IUIAutomation* uia = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(CUIAutomation), nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  __uuidof(IUIAutomation),
                                  (void**)&uia);
    if (FAILED(hr) || !uia) { CoUninitialize(); return results; }

    HWND hTray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!hTray) { uia->Release(); CoUninitialize(); return results; }

    IUIAutomationElement* trayElem = nullptr;
    hr = uia->ElementFromHandle(hTray, &trayElem);
    if (FAILED(hr) || !trayElem) {
        uia->Release(); CoUninitialize(); return results;
    }

    // Find all button-type elements under the taskbar
    // On Win11 these have control type UIA_ButtonControlTypeId (0xC350)
    // and names like "AppName pinned" or "AppName - N running windows"
    IUIAutomationCondition* condButton = nullptr;
    VARIANT vtButton; VariantInit(&vtButton);
    vtButton.vt = VT_I4; vtButton.lVal = UIA_ButtonControlTypeId;
    uia->CreatePropertyCondition(UIA_ControlTypePropertyId, vtButton, &condButton);

    if (condButton) {
        IUIAutomationElementArray* buttons = nullptr;
        hr = trayElem->FindAll(TreeScope_Descendants, condButton, &buttons);
        if (SUCCEEDED(hr) && buttons) {
            int count = 0;
            buttons->get_Length(&count);
            for (int i = 0; i < count; ++i) {
                IUIAutomationElement* btnElem = nullptr;
                buttons->GetElement(i, &btnElem);
                if (!btnElem) continue;

                BSTR bstrName = nullptr;
                btnElem->get_CurrentName(&bstrName);
                std::wstring name = bstrName ? bstrName : L"";
                if (bstrName) SysFreeString(bstrName);

                // Skip system tray buttons (no executable behind them)
                if (name.empty()) { btnElem->Release(); continue; }

                // Skip known system tray items
                if (name == L"Start" || name == L"Show Desktop" ||
                    name == L"Show Hidden Icons" || name == L"Touch Keyboard" ||
                    name == L"Clock" || name.find(L"Network") != std::wstring::npos ||
                    name.find(L"Volume") != std::wstring::npos ||
                    name.find(L"Power") != std::wstring::npos) {
                    btnElem->Release();
                    continue;
                }

                // Try to match this button to a running process
                std::wstring exePath, displayName;
                if (!MatchButtonToProcess(name, exePath, displayName)) {
                    btnElem->Release();
                    continue;
                }

                TaskbarButton btn;
                btn.exePath = exePath;
                btn.displayName = displayName;

                // Get the icon from the actual process's main window
                // For live taskbar icons we use ExtractIconExW on the exe
                btn.hIcon = ExtractSingleIcon(exePath, 0);

                btn.lnkPath = FindMatchingLnk(exePath);
                btn.hasEditableLnk = !btn.lnkPath.empty();
                results.push_back(std::move(btn));
                btnElem->Release();
            }
            buttons->Release();
        }
        condButton->Release();
    }

    trayElem->Release();
    uia->Release();
    CoUninitialize();

    // Deduplicate by exePath
    for (size_t i = 0; i < results.size(); ++i) {
        for (size_t j = i + 1; j < results.size();) {
            if (_wcsicmp(results[i].exePath.c_str(),
                         results[j].exePath.c_str()) == 0) {
                if (results[j].hasEditableLnk && !results[i].hasEditableLnk) {
                    results[i].hasEditableLnk = results[j].hasEditableLnk;
                    results[i].lnkPath = results[j].lnkPath;
                }
                results.erase(results.begin() + j);
            } else { ++j; }
        }
    }

    return results;
}

