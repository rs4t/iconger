#pragma once
#include <windows.h>
#include <map>
#include <string>
#include <vector>

// EXPERIMENTAL: custom icons for apps that are on the taskbar without being pinned.
// Two things are changed on each of the app's windows while it's open (and again for
// every new window), nothing on disk:
//  - the window icon (WM_SETICON): title bar and Alt+Tab;
//  - the window's app identity (AppUserModelID + relaunch icon, like Chrome does for
//    its profiles): the taskbar button. The taskbar picks its icon from the app's
//    identity, not from the window icon.
// It only lasts while Iconger runs.

/// An app with at least one window that has a taskbar button.
struct RunningApp {
    std::wstring exe;           // full path of the program
    std::wstring name;          // "Discord" (the exe's description), else the file name
    std::vector<HWND> windows;
    bool packaged = false;      // Store app hosted by ApplicationFrameHost: can't be changed this way
};

/// Apps with a taskbar button right now, grouped by program, excluding Iconger itself.
std::vector<RunningApp> EnumerateTaskbarApps();

/// True if `hwnd` gets its own taskbar button (visible, top-level, not a tool window, not cloaked).
bool HasTaskbarButton(HWND hwnd);

/// Full path of the program that owns the window, or empty.
std::wstring WindowExePath(HWND hwnd);

/// The program's FileDescription ("Discord"), or its file name without .exe.
std::wstring ExeDisplayName(const std::wstring& exe);

/// Which program gets which icon. Stored in %LOCALAPPDATA%\Iconger\window_icons.tsv.
class WindowIconRules {
public:
    void Load();
    bool Save() const;
    /// The .ico for this program, or nullptr. Paths compare case-insensitively.
    const std::wstring* Get(const std::wstring& exe) const;
    void Set(const std::wstring& exe, const std::wstring& ico);
    void Remove(const std::wstring& exe);
    const std::map<std::wstring, std::wstring>& All() const { return m_rules; } // lower-case exe -> ico
    static std::wstring Key(const std::wstring& exe);
private:
    std::map<std::wstring, std::wstring> m_rules;
};

/// Applies the rules to windows as they appear, and puts the original icons back.
/// Runs on the UI thread: the window-event hook is delivered through its message loop.
class WindowIconKeeper {
public:
    ~WindowIconKeeper() { Stop(); }
    void Start(const WindowIconRules* rules);
    /// Put every original icon back and stop watching.
    void Stop();
    bool Running() const { return m_hook != nullptr; }

    /// Go over every taskbar window now (after starting, or after a rule changed).
    void ApplyAll();
    /// Give the windows of `exe` their own icons back (its rule was removed).
    void RestoreApp(const std::wstring& exe);
    /// Re-apply where an app put its own icon back, forget closed windows. Call every ~2 s.
    void Tick();

    /// Programs whose windows refused the icon (e.g. running as administrator).
    const std::vector<std::wstring>& Blocked() const { return m_blocked; }

    /// A window's app identity for the taskbar (empty = not set on the window).
    struct Identity { std::wstring id, icon, command, name; };
    static bool ReadIdentity(HWND hwnd, Identity& out);
    static bool WriteIdentity(HWND hwnd, const Identity& identity);
    /// The app ID Iconger gives the windows of `exe` shown with `ico` (unique per pair,
    /// so the taskbar can't reuse an icon it cached for an earlier choice).
    static std::wstring AppIdFor(const std::wstring& exe, const std::wstring& ico);

private:
    struct Applied {
        std::wstring exe;
        HICON origBig = nullptr, origSmall = nullptr;
        HICON bigIcon = nullptr, smallIcon = nullptr;
        Identity origIdentity;
        std::wstring appId;       // ours
    };
    static void RestoreWindow(HWND hwnd, const Applied& a);
    struct Icons { HICON bigIcon = nullptr; HICON smallIcon = nullptr; };

    void Apply(HWND hwnd);
    const Icons* IconsFor(const std::wstring& ico, UINT dpi);
    static void CALLBACK OnWinEvent(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);

    const WindowIconRules* m_rules = nullptr;
    HWINEVENTHOOK m_hook = nullptr;
    std::map<HWND, Applied> m_applied;
    std::map<std::wstring, Icons> m_icons; // "<ico>|<dpi>" -> loaded icons, owned
    std::vector<std::wstring> m_blocked;
};

/// Start Iconger with Windows (HKCU Run key, "--background") or stop doing so.
bool SetStartWithWindows(bool enable, const std::wstring& exe);
bool StartsWithWindows();
