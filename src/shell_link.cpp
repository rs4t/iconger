#include "shell_link.h"
#include "app_paths.h"
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <cwctype>

using Microsoft::WRL::ComPtr;

static bool LoadLink(const std::wstring& lnkPath, DWORD mode,
                     ComPtr<IShellLinkW>& link, ComPtr<IPersistFile>& file)
{
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))))
        return false;
    if (FAILED(link.As(&file))) return false;
    return SUCCEEDED(file->Load(lnkPath.c_str(), mode));
}

std::wstring GetPinnedShortcutsFolder()
{
    std::wstring dir;
    PWSTR roaming = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)))
        dir = roaming;
    CoTaskMemFree(roaming);
    if (dir.empty()) dir = ExpandEnv(L"%APPDATA%");
    return dir + L"\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar";
}

// "Firefox (2)" -> "Firefox"
static std::wstring StripRepinSuffix(const std::wstring& name)
{
    size_t open = name.rfind(L" (");
    if (open == std::wstring::npos || name.back() != L')' || open + 3 >= name.size()) return name;
    for (size_t i = open + 2; i + 1 < name.size(); ++i)
        if (!iswdigit(name[i])) return name;
    return name.substr(0, open);
}

bool BlobMentionsFile(const std::vector<unsigned char>& blob, const std::wstring& lnkFileName)
{
    std::wstring needle = lnkFileName;
    if (needle.empty()) return false;
    CharLowerBuffW(needle.data(), (DWORD)needle.size());
    // names are UTF-16 inside the PIDLs, at arbitrary (odd or even) byte offsets
    for (size_t align = 0; align < 2; ++align) {
        if (blob.size() < align + 2) continue;
        std::wstring hay((blob.size() - align) / 2, L' ');
        memcpy(hay.data(), blob.data() + align, hay.size() * sizeof(wchar_t));
        CharLowerBuffW(hay.data(), (DWORD)hay.size());
        if (hay.find(needle) != std::wstring::npos) return true;
    }
    return false;
}

static bool ReadTaskbandBlob(std::vector<unsigned char>& blob)
{
    const wchar_t* key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Taskband";
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, key, L"Favorites", RRF_RT_REG_BINARY, nullptr, nullptr, &size) != ERROR_SUCCESS || !size)
        return false;
    blob.resize(size);
    return RegGetValueW(HKEY_CURRENT_USER, key, L"Favorites", RRF_RT_REG_BINARY, nullptr, blob.data(), &size) == ERROR_SUCCESS;
}

bool ReadShortcut(const std::wstring& lnkPath, PinnedShortcut& sc)
{
    ComPtr<IShellLinkW> link;
    ComPtr<IPersistFile> file;
    if (!LoadLink(lnkPath, STGM_READ, link, file)) return false;

    sc = {};
    sc.lnkPath = lnkPath;
    sc.displayName = StripRepinSuffix(FileStem(lnkPath));

    // Deliberately no IShellLink::Resolve(): it can hit the network / search the
    // disk for moved targets, which made the old enumeration slow.
    wchar_t buf[MAX_PATH] = {};
    if (SUCCEEDED(link->GetPath(buf, MAX_PATH, nullptr, SLGP_RAWPATH)) && buf[0])
        sc.targetPath = ExpandEnv(buf);

    wchar_t iconBuf[MAX_PATH] = {};
    int idx = 0;
    if (SUCCEEDED(link->GetIconLocation(iconBuf, MAX_PATH, &idx)) && iconBuf[0]) {
        sc.iconPath = iconBuf;
        sc.iconIndex = idx;
    }
    return true;
}

std::vector<PinnedShortcut> EnumeratePinnedShortcuts()
{
    std::vector<PinnedShortcut> results;
    std::wstring folder = GetPinnedShortcutsFolder();

    // If the blob can't be read we can't tell leftovers apart, so treat all as live.
    std::vector<unsigned char> blob;
    bool haveBlob = ReadTaskbandBlob(blob);

    WIN32_FIND_DATAW ffd = {};
    HANDLE hFind = FindFirstFileW((folder + L"\\*.lnk").c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return results;
    do {
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        PinnedShortcut sc;
        if (ReadShortcut(folder + L"\\" + ffd.cFileName, sc)) {
            sc.onTaskbar = !haveBlob || BlobMentionsFile(blob, ffd.cFileName);
            results.push_back(std::move(sc));
        }
    } while (FindNextFileW(hFind, &ffd));
    FindClose(hFind);

    std::sort(results.begin(), results.end(), [](const PinnedShortcut& a, const PinnedShortcut& b) {
        if (a.onTaskbar != b.onTaskbar) return a.onTaskbar;
        return CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE,
                               a.displayName.c_str(), -1, b.displayName.c_str(), -1,
                               nullptr, nullptr, 0) == CSTR_LESS_THAN;
    });
    return results;
}

bool SetShortcutIcon(const std::wstring& lnkPath, const std::wstring& newIconPath, int newIconIndex)
{
    ComPtr<IShellLinkW> link;
    ComPtr<IPersistFile> file;
    if (!LoadLink(lnkPath, STGM_READWRITE, link, file)) return false;

    if (FAILED(link->SetIconLocation(newIconPath.c_str(), newIconIndex))) return false;

    // Installer-made shortcuts often carry an EXP_SZ_ICON block ("%ProgramFiles%\...").
    // When present it wins over the plain icon location, so a stale one would
    // silently keep the old icon. Drop it unless the new path itself uses %VARS%.
    ComPtr<IShellLinkDataList> data;
    if (SUCCEEDED(link.As(&data)) && newIconPath.find(L'%') == std::wstring::npos) {
        DWORD flags = 0;
        if (SUCCEEDED(data->GetFlags(&flags)) && (flags & SLDF_HAS_EXP_ICON_SZ)) {
            data->RemoveDataBlock(EXP_SZ_ICON_SIG);
            data->SetFlags(flags & ~SLDF_HAS_EXP_ICON_SZ);
        }
    }

    if (FAILED(file->Save(lnkPath.c_str(), TRUE))) return false;
    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, lnkPath.c_str(), nullptr);
    return true;
}

void ResolveShortcutIcon(const PinnedShortcut& sc, std::wstring& path, int& index)
{
    if (!sc.iconPath.empty()) {
        path = ExpandEnv(sc.iconPath);
        index = sc.iconIndex;
    } else {
        path = sc.targetPath;
        index = 0;
    }
}
