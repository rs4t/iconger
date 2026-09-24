#include "shell_link.h"
#include "app_paths.h"
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <propkey.h>
#include <propvarutil.h>
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

static std::wstring ReadExpIconBlock(const ComPtr<IShellLinkW>& link)
{
    ComPtr<IShellLinkDataList> data;
    DWORD flags = 0;
    if (FAILED(link.As(&data)) || FAILED(data->GetFlags(&flags)) || !(flags & SLDF_HAS_EXP_ICON_SZ)) return {};
    void* block = nullptr;
    std::wstring out;
    if (SUCCEEDED(data->CopyDataBlock(EXP_SZ_ICON_SIG, &block)) && block) {
        const auto* exp = static_cast<const EXP_SZ_LINK*>(block);
        out.assign(exp->swzTarget, wcsnlen(exp->swzTarget, MAX_PATH));
        LocalFree(block);
    }
    return out;
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
    // Installer shortcuts keep the real icon path as "%ProgramFiles%\..." in an
    // EXP_SZ_ICON block, which wins over the plain location. Report that one, so a
    // backup restores exactly what was there.
    std::wstring expIcon = ReadExpIconBlock(link);
    if (!expIcon.empty()) sc.iconPath = expIcon;

    ComPtr<IPropertyStore> props;
    if (SUCCEEDED(link.As(&props))) {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        if (SUCCEEDED(props->GetValue(PKEY_AppUserModel_ID, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal)
            sc.aumid = pv.pwszVal;
        PropVariantClear(&pv);
    }
    return true;
}

std::vector<std::wstring> ExtractPinnedAppIds(const std::vector<unsigned char>& blob)
{
    // "<PackageFamilyName>!<AppId>", e.g. Claude_pzs8sxrjxfjjc!Claude. The family name is
    // "<name>_<13-char publisher id>", which keeps random '!' in other data from matching.
    auto isIdChar = [](wchar_t c) { return iswalnum(c) || c == L'.' || c == L'_' || c == L'-'; };
    std::vector<std::wstring> ids;
    for (size_t align = 0; align < 2; ++align) {
        if (blob.size() < align + 2) continue;
        std::wstring hay((blob.size() - align) / 2, L' ');
        memcpy(hay.data(), blob.data() + align, hay.size() * sizeof(wchar_t));
        for (size_t bang = hay.find(L'!'); bang != std::wstring::npos; bang = hay.find(L'!', bang + 1)) {
            size_t a = bang, b = bang + 1;
            while (a > 0 && isIdChar(hay[a - 1])) --a;
            while (b < hay.size() && isIdChar(hay[b])) ++b;
            std::wstring family = hay.substr(a, bang - a), app = hay.substr(bang + 1, b - bang - 1);
            size_t us = family.rfind(L'_');
            if (app.empty() || us == std::wstring::npos || family.size() - us - 1 != 13) continue;
            std::wstring id = family + L"!" + app;
            if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
        }
    }
    return ids;
}

std::wstring AppsFolderPath(const std::wstring& aumid) { return L"shell:AppsFolder\\" + aumid; }

std::wstring AppDisplayName(const std::wstring& aumid)
{
    ComPtr<IShellItem> item;
    if (FAILED(SHCreateItemFromParsingName(AppsFolderPath(aumid).c_str(), nullptr, IID_PPV_ARGS(&item))))
        return {};
    PWSTR name = nullptr;
    std::wstring out;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name)) && name) out = name;
    CoTaskMemFree(name);
    return out;
}

std::wstring AppShortcutsFolder(bool create)
{
    std::wstring dir;
    PWSTR programs = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &programs))) dir = programs;
    CoTaskMemFree(programs);
    if (dir.empty()) dir = ExpandEnv(L"%APPDATA%\\Microsoft\\Windows\\Start Menu\\Programs");
    dir += L"\\Iconger";
    if (create) CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring StartMenuShortcutPath()
{
    return AppShortcutsFolder(false) + L".lnk"; // ...\Programs\Iconger.lnk
}

bool EnsureStartMenuShortcut(const std::wstring& exe)
{
    std::wstring lnk = StartMenuShortcutPath();
    PinnedShortcut current;
    if (ReadShortcut(lnk, current) && _wcsicmp(current.targetPath.c_str(), exe.c_str()) == 0) return true;

    ComPtr<IShellLinkW> link;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return false;
    size_t slash = exe.find_last_of(L"\\/");
    link->SetPath(exe.c_str());
    if (slash != std::wstring::npos) link->SetWorkingDirectory(exe.substr(0, slash).c_str());
    link->SetDescription(L"Change the icons of the apps pinned to your taskbar");
    link->SetIconLocation(exe.c_str(), 0);
    ComPtr<IPersistFile> file;
    if (FAILED(link.As(&file)) || FAILED(file->Save(lnk.c_str(), TRUE))) return false;
    SHChangeNotify(SHCNE_CREATE, SHCNF_PATHW, lnk.c_str(), nullptr);
    return true;
}

void RemoveStartMenuShortcut()
{
    std::wstring lnk = StartMenuShortcutPath();
    if (DeleteFileW(lnk.c_str())) SHChangeNotify(SHCNE_DELETE, SHCNF_PATHW, lnk.c_str(), nullptr);
}

bool CreateAppShortcut(const std::wstring& lnkPath, const std::wstring& aumid,
                       const std::wstring& iconPath, int iconIndex)
{
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHParseDisplayName(AppsFolderPath(aumid).c_str(), nullptr, &pidl, 0, nullptr))) return false;
    ComPtr<IShellLinkW> link;
    bool ok = SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) &&
              SUCCEEDED(link->SetIDList(pidl));
    CoTaskMemFree(pidl);
    if (!ok) return false;
    if (!iconPath.empty()) link->SetIconLocation(iconPath.c_str(), iconIndex);

    ComPtr<IPropertyStore> props;
    if (FAILED(link.As(&props))) return false;
    PROPVARIANT pv;
    if (FAILED(InitPropVariantFromString(aumid.c_str(), &pv))) return false;
    ok = SUCCEEDED(props->SetValue(PKEY_AppUserModel_ID, pv)) && SUCCEEDED(props->Commit());
    PropVariantClear(&pv);

    ComPtr<IPersistFile> file;
    return ok && SUCCEEDED(link.As(&file)) && SUCCEEDED(file->Save(lnkPath.c_str(), TRUE));
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
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            PinnedShortcut sc;
            if (ReadShortcut(folder + L"\\" + ffd.cFileName, sc)) {
                sc.onTaskbar = !haveBlob || BlobMentionsFile(blob, ffd.cFileName);
                results.push_back(std::move(sc));
            }
        } while (FindNextFileW(hFind, &ffd));
        FindClose(hFind);
    }

    // Packaged apps pinned straight from Start have no .lnk; skip ones an Iconger
    // shortcut (same AppUserModelID) already stands in for.
    if (haveBlob) {
        for (const std::wstring& id : ExtractPinnedAppIds(blob)) {
            bool covered = std::any_of(results.begin(), results.end(), [&](const PinnedShortcut& s) {
                return s.onTaskbar && _wcsicmp(s.aumid.c_str(), id.c_str()) == 0; });
            std::wstring name = covered ? L"" : AppDisplayName(id);
            if (covered || name.empty()) continue; // not installed any more
            PinnedShortcut sc;
            sc.displayName = name;
            sc.aumid = id;
            sc.packaged = true;
            results.push_back(std::move(sc));
        }
    }

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

    // The plain icon location always gets the expanded path. A "%VARS%" path also goes
    // into an EXP_SZ_ICON block (that is how installers store them, and it wins when
    // present). Any existing block is dropped first: a stale one would silently keep
    // the old icon.
    std::wstring expanded = ExpandEnv(newIconPath);
    if (FAILED(link->SetIconLocation(expanded.c_str(), newIconIndex))) return false;

    ComPtr<IShellLinkDataList> data;
    DWORD flags = 0;
    if (SUCCEEDED(link.As(&data)) && SUCCEEDED(data->GetFlags(&flags))) {
        const DWORD oldFlags = flags;
        if (flags & SLDF_HAS_EXP_ICON_SZ) {
            data->RemoveDataBlock(EXP_SZ_ICON_SIG);
            flags &= ~SLDF_HAS_EXP_ICON_SZ;
        }
        if (newIconPath.find(L'%') != std::wstring::npos && newIconPath.size() < MAX_PATH) {
            EXP_SZ_LINK exp = {};
            exp.cbSize = sizeof(exp);
            exp.dwSignature = EXP_SZ_ICON_SIG;
            wcsncpy_s(exp.swzTarget, newIconPath.c_str(), _TRUNCATE);
            WideCharToMultiByte(CP_ACP, 0, newIconPath.c_str(), -1, exp.szTarget, MAX_PATH, nullptr, nullptr);
            if (SUCCEEDED(data->AddDataBlock(&exp))) flags |= SLDF_HAS_EXP_ICON_SZ;
            else link->SetIconLocation(newIconPath.c_str(), newIconIndex); // no block support: keep the text as is
        }
        if (flags != oldFlags) data->SetFlags(flags);
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
