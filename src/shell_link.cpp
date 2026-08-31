#include "shell_link.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <objbase.h>
#include <initguid.h>
#include <cstdio>
#include <array>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

// ----------------------------------------------------------------------
// Minimal COM smart pointer (to avoid ATL dependency)
// ----------------------------------------------------------------------
template<typename T>
class ComPtr {
    T* ptr = nullptr;
public:
    ComPtr() noexcept : ptr(nullptr) {}
    explicit ComPtr(T* p) noexcept : ptr(p) { if (ptr) ptr->AddRef(); }
    ComPtr(const ComPtr&) noexcept = delete;
    ComPtr(ComPtr&& other) noexcept : ptr(other.ptr) { other.ptr = nullptr; }
    ~ComPtr() { if (ptr) ptr->Release(); }

    ComPtr& operator=(T* p) noexcept {
        if (ptr) ptr->Release();
        ptr = p;
        return *this;
    }
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            if (ptr) ptr->Release();
            ptr = other.ptr;
            other.ptr = nullptr;
        }
        return *this;
    }

    T* operator->() const noexcept { return ptr; }
    T** AddressOf() noexcept { return &ptr; }
    T* Get() const noexcept { return ptr; }
    void** Void() noexcept { return reinterpret_cast<void**>(&ptr); }
    explicit operator bool() const noexcept { return ptr != nullptr; }

    template<typename Q>
    HRESULT As(Q** pp) const {
        return ptr ? ptr->QueryInterface(__uuidof(Q), (void**)pp) : E_POINTER;
    }
};

// ----------------------------------------------------------------------
// Internal helpers
// ----------------------------------------------------------------------

static std::wstring GetLocalAppDataFolder() {
    wchar_t buf[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr,
                                   SHGFP_TYPE_CURRENT, buf))) {
        return std::wstring(buf);
    }
    std::wstring local;
    local.resize(MAX_PATH, L'\0');
    DWORD ret = GetEnvironmentVariableW(L"LOCALAPPDATA", &local[0], (DWORD)local.size());
    if (ret > 0 && ret < local.size()) { local.resize(ret); return local; }
    std::wstring user;
    user.resize(MAX_PATH, L'\0');
    ret = GetEnvironmentVariableW(L"USERPROFILE", &user[0], (DWORD)user.size());
    if (ret > 0 && ret < user.size()) { user.resize(ret); return user + L"\\AppData\\Local"; }
    return L"C:\\Users\\Default\\AppData\\Local";
}

static std::wstring GetAppDataFolder() {
    wchar_t buf[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr,
                                   SHGFP_TYPE_CURRENT, buf))) {
        return std::wstring(buf);
    }
    std::wstring appdata;
    appdata.resize(MAX_PATH, L'\0');
    DWORD ret = GetEnvironmentVariableW(L"APPDATA", &appdata[0], (DWORD)appdata.size());
    if (ret > 0 && ret < appdata.size()) { appdata.resize(ret); return appdata; }
    return GetLocalAppDataFolder() + L"\\Microsoft\\Windows";
}
// ----------------------------------------------------------------------
// Public API
// ----------------------------------------------------------------------

std::wstring GetPinnedShortcutsFolder() {
    return GetAppDataFolder() + L"\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar";
}

PinnedShortcut ReadShortcut(const std::wstring& lnkPath) {
    PinnedShortcut sc;
    sc.filePath = lnkPath;
    sc.valid = false;

    wchar_t fname[MAX_PATH] = {};
    wchar_t ext[MAX_PATH] = {};
    _wsplitpath_s(lnkPath.c_str(), nullptr, 0, nullptr, 0,
                  fname, MAX_PATH, ext, MAX_PATH);
    sc.displayName = fname;

    ComPtr<IShellLinkW> pShellLink;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IShellLinkW, pShellLink.Void());
    if (FAILED(hr)) {
#ifdef _DEBUG
        std::array<wchar_t, 64> buf{};
        swprintf_s(buf.data(), buf.size(), L"Iconger: CoCreateInstance(ShellLink) failed: 0x%08X\n", hr);
        OutputDebugStringW(buf.data());
#endif
        return sc;
    }

    ComPtr<IPersistFile> pPersistFile;
    hr = pShellLink->QueryInterface(IID_IPersistFile,
                                    reinterpret_cast<void**>(pPersistFile.AddressOf()));
    if (FAILED(hr)) return sc;

    hr = pPersistFile->Load(lnkPath.c_str(), STGM_READ);
    if (FAILED(hr)) return sc;

    hr = pShellLink->Resolve(nullptr, SLR_NO_UI | SLR_NOUPDATE);

    wchar_t targetBuf[MAX_PATH] = {};
    WIN32_FIND_DATAW wfd = {};
    hr = pShellLink->GetPath(targetBuf, MAX_PATH, &wfd, SLGP_UNCPRIORITY);
    if (SUCCEEDED(hr) && wcslen(targetBuf) > 0)
        sc.targetPath = targetBuf;

    wchar_t iconBuf[MAX_PATH] = {};
    int iconIdx = 0;
    hr = pShellLink->GetIconLocation(iconBuf, MAX_PATH, &iconIdx);
    if (SUCCEEDED(hr) && wcslen(iconBuf) > 0) {
        sc.iconPath = iconBuf;
        sc.iconIndex = iconIdx;
    } else {
        sc.iconPath = sc.targetPath;
        sc.iconIndex = 0;
    }

    wchar_t descBuf[1024] = {};
    hr = pShellLink->GetDescription(descBuf, (int)std::size(descBuf));
    if (SUCCEEDED(hr) && wcslen(descBuf) > 0)
        sc.displayName = descBuf;

    sc.valid = true;
    return sc;
}

std::vector<PinnedShortcut> EnumeratePinnedShortcuts() {
    std::vector<PinnedShortcut> results;
    std::wstring folder = GetPinnedShortcutsFolder();
    std::wstring searchPath = folder + L"\\*.lnk";

    WIN32_FIND_DATAW ffd = {};
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE)
        return results;

    do {
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::wstring fullPath = folder + L"\\" + ffd.cFileName;
            PinnedShortcut sc = ReadShortcut(fullPath);
            results.push_back(std::move(sc));
        }
    } while (FindNextFileW(hFind, &ffd) != 0);

    FindClose(hFind);
    return results;
}

bool SetShortcutIcon(const std::wstring& lnkPath,
                     const std::wstring& newIconPath,
                     int newIconIndex) {
    ComPtr<IShellLinkW> pShellLink;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IShellLinkW, pShellLink.Void());
    if (FAILED(hr)) return false;

    ComPtr<IPersistFile> pPersistFile;
    hr = pShellLink->QueryInterface(IID_IPersistFile,
                                    reinterpret_cast<void**>(pPersistFile.AddressOf()));
    if (FAILED(hr)) return false;

    hr = pPersistFile->Load(lnkPath.c_str(), STGM_READWRITE);
    if (FAILED(hr)) return false;

    hr = pShellLink->SetIconLocation(newIconPath.c_str(), newIconIndex);
    if (FAILED(hr)) return false;

    hr = pPersistFile->Save(lnkPath.c_str(), TRUE);
    if (FAILED(hr)) return false;

    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, lnkPath.c_str(), nullptr);
    return true;
}

bool ResetShortcutIcon(const std::wstring& lnkPath) {
    // Calling SetIconLocation with an empty path and index 0 tells the shell
    // to use the target file's own default icon (i.e., clear the override).
    return SetShortcutIcon(lnkPath, L"", 0);
}
