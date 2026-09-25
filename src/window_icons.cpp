#include "window_icons.h"
#include "app_paths.h"
#include <dwmapi.h>
#include <algorithm>
#include <cwchar>

namespace {

constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kRunValue = L"Iconger";

WindowIconKeeper* g_keeper = nullptr; // the hook callback has no user pointer

std::wstring RulesFile() { return DataDir() + L"\\window_icons.tsv"; }

std::wstring Lower(std::wstring s)
{
    if (!s.empty()) CharLowerBuffW(s.data(), (DWORD)s.size());
    return s;
}

bool IsShellWindow(HWND hwnd)
{
    wchar_t cls[64] = {};
    GetClassNameW(hwnd, cls, (int)std::size(cls));
    for (const wchar_t* c : { L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd" })
        if (_wcsicmp(cls, c) == 0) return true;
    return false;
}

HICON CurrentIcon(HWND hwnd, WPARAM which)
{
    DWORD_PTR icon = 0;
    SendMessageTimeoutW(hwnd, WM_GETICON, which, 0, SMTO_ABORTIFHUNG, 150, &icon);
    return (HICON)icon;
}

} // namespace

bool HasTaskbarButton(HWND hwnd)
{
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd) return false;
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return false;
    if (GetWindow(hwnd, GW_OWNER) && !(ex & WS_EX_APPWINDOW)) return false;
    DWORD cloaked = 0; // e.g. windows on another virtual desktop, suspended Store apps
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return false;
    return !IsShellWindow(hwnd);
}

std::wstring WindowExePath(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE proc = pid ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) : nullptr;
    if (!proc) return {};
    wchar_t buf[MAX_PATH * 2];
    DWORD len = (DWORD)std::size(buf);
    std::wstring out;
    if (QueryFullProcessImageNameW(proc, 0, buf, &len)) out.assign(buf, len);
    CloseHandle(proc);
    return out;
}

std::wstring ExeDisplayName(const std::wstring& exe)
{
    DWORD dummy = 0, size = GetFileVersionInfoSizeW(exe.c_str(), &dummy);
    if (size) {
        std::vector<uint8_t> data(size);
        struct Lang { WORD lang, codepage; }* langs = nullptr;
        UINT langBytes = 0;
        if (GetFileVersionInfoW(exe.c_str(), 0, size, data.data()) &&
            VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", (void**)&langs, &langBytes) && langBytes >= sizeof(Lang)) {
            wchar_t path[64];
            swprintf_s(path, L"\\StringFileInfo\\%04x%04x\\FileDescription", langs[0].lang, langs[0].codepage);
            wchar_t* desc = nullptr;
            UINT descLen = 0;
            if (VerQueryValueW(data.data(), path, (void**)&desc, &descLen) && desc && descLen > 1) {
                std::wstring d(desc);
                while (!d.empty() && iswspace(d.back())) d.pop_back();
                if (!d.empty()) return d;
            }
        }
    }
    return FileStem(exe);
}

std::vector<RunningApp> EnumerateTaskbarApps()
{
    struct Ctx { std::vector<RunningApp> apps; DWORD self; };
    Ctx ctx{ {}, GetCurrentProcessId() };
    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto& c = *reinterpret_cast<Ctx*>(lp);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == c.self || !HasTaskbarButton(hwnd) || GetWindowTextLengthW(hwnd) == 0) return TRUE;
        std::wstring exe = WindowExePath(hwnd);
        if (exe.empty()) return TRUE; // e.g. a process running as administrator
        // Store apps are hosted by ApplicationFrameHost; their icon comes from the package
        if (_wcsicmp(FileStem(exe).c_str(), L"ApplicationFrameHost") == 0) return TRUE;
        auto it = std::find_if(c.apps.begin(), c.apps.end(),
                               [&](const RunningApp& a) { return _wcsicmp(a.exe.c_str(), exe.c_str()) == 0; });
        if (it == c.apps.end()) {
            RunningApp a;
            a.exe = exe;
            a.name = ExeDisplayName(exe);
            c.apps.push_back(std::move(a));
            it = c.apps.end() - 1;
        }
        it->windows.push_back(hwnd);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    std::sort(ctx.apps.begin(), ctx.apps.end(), [](const RunningApp& a, const RunningApp& b) {
        return CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE, a.name.c_str(), -1,
                               b.name.c_str(), -1, nullptr, nullptr, 0) == CSTR_LESS_THAN;
    });
    return ctx.apps;
}

// ============================================================================
// Rules
// ============================================================================

std::wstring WindowIconRules::Key(const std::wstring& exe) { return Lower(exe); }

void WindowIconRules::Load()
{
    m_rules.clear();
    std::vector<uint8_t> bytes;
    if (!ReadWholeFile(RulesFile(), bytes)) return;
    std::wstring text = Utf8ToWide(std::string(bytes.begin(), bytes.end()));
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find(L'\n', pos);
        if (eol == std::wstring::npos) eol = text.size();
        std::wstring line = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        size_t tab = line.find(L'\t'); // exe \t ico
        if (tab == std::wstring::npos || tab == 0 || tab + 1 >= line.size()) continue;
        m_rules[Key(line.substr(0, tab))] = line.substr(tab + 1);
    }
}

bool WindowIconRules::Save() const
{
    std::wstring text;
    for (const auto& [exe, ico] : m_rules) text += exe + L"\t" + ico + L"\n";
    std::string utf8 = WideToUtf8(text);
    std::wstring tmp = RulesFile() + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = utf8.empty() || (WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, nullptr) && written == utf8.size());
    CloseHandle(h);
    return ok && MoveFileExW(tmp.c_str(), RulesFile().c_str(), MOVEFILE_REPLACE_EXISTING);
}

const std::wstring* WindowIconRules::Get(const std::wstring& exe) const
{
    auto it = m_rules.find(Key(exe));
    return it == m_rules.end() ? nullptr : &it->second;
}

void WindowIconRules::Set(const std::wstring& exe, const std::wstring& ico) { m_rules[Key(exe)] = ico; }
void WindowIconRules::Remove(const std::wstring& exe) { m_rules.erase(Key(exe)); }

// ============================================================================
// Keeper
// ============================================================================

void WindowIconKeeper::Start(const WindowIconRules* rules)
{
    m_rules = rules;
    if (!m_hook) {
        g_keeper = this;
        // out-of-context: delivered on this thread through its message loop, no DLL injection
        m_hook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, OnWinEvent, 0, 0,
                                 WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    }
    ApplyAll();
}

void WindowIconKeeper::Stop()
{
    if (m_hook) UnhookWinEvent(m_hook);
    m_hook = nullptr;
    if (g_keeper == this) g_keeper = nullptr;
    // Put the originals back before our icons are destroyed, or the windows would
    // be left pointing at freed icons.
    for (auto& [hwnd, a] : m_applied) {
        if (!IsWindow(hwnd)) continue;
        DWORD_PTR r;
        SendMessageTimeoutW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)a.origBig, SMTO_ABORTIFHUNG, 200, &r);
        SendMessageTimeoutW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)a.origSmall, SMTO_ABORTIFHUNG, 200, &r);
    }
    m_applied.clear();
    for (auto& [key, ic] : m_icons) {
        if (ic.big) DestroyIcon(ic.big);
        if (ic.small) DestroyIcon(ic.small);
    }
    m_icons.clear();
}

void CALLBACK WindowIconKeeper::OnWinEvent(HWINEVENTHOOK, DWORD, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD)
{
    if (idObject == OBJID_WINDOW && idChild == CHILDID_SELF && g_keeper) g_keeper->Apply(hwnd);
}

const WindowIconKeeper::Icons* WindowIconKeeper::IconsFor(const std::wstring& ico, UINT dpi)
{
    std::wstring key = ico + L"|" + std::to_wstring(dpi);
    auto it = m_icons.find(key);
    if (it != m_icons.end()) return &it->second;
    Icons ic;
    int big = GetSystemMetricsForDpi(SM_CXICON, dpi), small = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
    ic.big = (HICON)LoadImageW(nullptr, ico.c_str(), IMAGE_ICON, big, big, LR_LOADFROMFILE);
    ic.small = (HICON)LoadImageW(nullptr, ico.c_str(), IMAGE_ICON, small, small, LR_LOADFROMFILE);
    if (!ic.big || !ic.small) {
        if (ic.big) DestroyIcon(ic.big);
        if (ic.small) DestroyIcon(ic.small);
        return nullptr;
    }
    return &(m_icons[key] = ic);
}

void WindowIconKeeper::Apply(HWND hwnd)
{
    if (!m_rules || !HasTaskbarButton(hwnd)) return;
    auto it = m_applied.find(hwnd);
    std::wstring exe = it != m_applied.end() ? it->second.exe : WindowExePath(hwnd);
    if (exe.empty()) return;
    const std::wstring* ico = m_rules->Get(exe);
    if (!ico) return;
    const Icons* ic = IconsFor(*ico, GetDpiForWindow(hwnd));
    if (!ic) return;
    if (it != m_applied.end() && it->second.big == ic->big && CurrentIcon(hwnd, ICON_BIG) == ic->big) return;

    Applied a;
    if (it != m_applied.end()) {
        a = it->second; // keep the true originals when re-applying
    } else {
        a.exe = WindowIconRules::Key(exe);
        a.origBig = CurrentIcon(hwnd, ICON_BIG);
        a.origSmall = CurrentIcon(hwnd, ICON_SMALL);
    }
    DWORD_PTR r;
    SetLastError(0);
    if (!SendMessageTimeoutW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)ic->big, SMTO_ABORTIFHUNG, 200, &r)) {
        // Windows won't let a normal program change the windows of one running as administrator
        if (GetLastError() == ERROR_ACCESS_DENIED &&
            std::find(m_blocked.begin(), m_blocked.end(), a.exe) == m_blocked.end())
            m_blocked.push_back(a.exe);
        return;
    }
    SendMessageTimeoutW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)ic->small, SMTO_ABORTIFHUNG, 200, &r);
    a.big = ic->big;
    a.small = ic->small;
    m_applied[hwnd] = a;
}

void WindowIconKeeper::ApplyAll()
{
    if (!m_rules || m_rules->All().empty()) return;
    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        reinterpret_cast<WindowIconKeeper*>(lp)->Apply(hwnd);
        return TRUE;
    }, reinterpret_cast<LPARAM>(this));
}

void WindowIconKeeper::RestoreApp(const std::wstring& exe)
{
    std::wstring key = WindowIconRules::Key(exe);
    for (auto it = m_applied.begin(); it != m_applied.end();) {
        if (it->second.exe != key) { ++it; continue; }
        if (IsWindow(it->first)) {
            DWORD_PTR r;
            SendMessageTimeoutW(it->first, WM_SETICON, ICON_BIG, (LPARAM)it->second.origBig, SMTO_ABORTIFHUNG, 200, &r);
            SendMessageTimeoutW(it->first, WM_SETICON, ICON_SMALL, (LPARAM)it->second.origSmall, SMTO_ABORTIFHUNG, 200, &r);
        }
        it = m_applied.erase(it);
    }
    m_blocked.erase(std::remove(m_blocked.begin(), m_blocked.end(), key), m_blocked.end());
}

void WindowIconKeeper::Tick()
{
    if (!m_hook) return;
    for (auto it = m_applied.begin(); it != m_applied.end();)
        it = IsWindow(it->first) ? std::next(it) : m_applied.erase(it);
    // Re-applies where an app set its own icon again, and catches windows that got
    // their taskbar button after the "shown" event (e.g. untitled at first).
    ApplyAll();
}

// ============================================================================
// Start with Windows
// ============================================================================

bool SetStartWithWindows(bool enable, const std::wstring& exe)
{
    if (!enable) {
        LSTATUS s = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue);
        return s == ERROR_SUCCESS || s == ERROR_FILE_NOT_FOUND;
    }
    std::wstring cmd = L"\"" + exe + L"\" --background";
    return RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, REG_SZ, cmd.c_str(),
                           (DWORD)((cmd.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

bool StartsWithWindows()
{
    DWORD size = 0;
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, nullptr, &size) == ERROR_SUCCESS;
}
