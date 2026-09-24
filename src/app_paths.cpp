#include "app_paths.h"
#include <windows.h>
#include <shlobj.h>
#include <cwctype>

std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), len);
    return out;
}

std::wstring ExpandEnv(const std::wstring& s)
{
    if (s.find(L'%') == std::wstring::npos) return s;
    DWORD need = ExpandEnvironmentStringsW(s.c_str(), nullptr, 0);
    if (!need) return s;
    std::wstring out(need, L'\0');
    DWORD got = ExpandEnvironmentStringsW(s.c_str(), out.data(), need);
    if (!got || got > need) return s;
    out.resize(got - 1);
    return out;
}

std::wstring LowerExt(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    size_t dot = path.rfind(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return {};
    std::wstring ext = path.substr(dot);
    for (auto& c : ext) c = (wchar_t)towlower(c);
    return ext;
}

std::wstring FileStem(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    size_t dot = name.rfind(L'.');
    return dot == std::wstring::npos || dot == 0 ? name : name.substr(0, dot);
}

bool FileExists(const std::wstring& path)
{
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool ReadWholeFile(const std::wstring& path, std::vector<uint8_t>& out)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart < (256ll << 20);
    if (ok) {
        out.resize((size_t)size.QuadPart);
        DWORD read = 0;
        ok = out.empty() || (ReadFile(h, out.data(), (DWORD)out.size(), &read, nullptr) && read == out.size());
    }
    CloseHandle(h);
    return ok;
}

static std::wstring g_dataDirOverride;

void SetDataDirForTests(const std::wstring& dir) { g_dataDirOverride = dir; }

std::wstring DataDir()
{
    std::wstring dir = g_dataDirOverride;
    if (dir.empty()) {
        PWSTR local = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
            dir = std::wstring(local) + L"\\Iconger";
        }
        CoTaskMemFree(local);
        if (dir.empty()) dir = ExpandEnv(L"%LOCALAPPDATA%\\Iconger");
    }
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring IconsDir()
{
    std::wstring dir = DataDir() + L"\\icons";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}
