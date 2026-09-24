#pragma once
#include <string>
#include <vector>

/// One pinned taskbar shortcut (.lnk in the User Pinned\TaskBar folder).
struct PinnedShortcut {
    std::wstring lnkPath;      // Full path to the .lnk file
    std::wstring displayName;  // File name without .lnk and without a " (2)" re-pin suffix
    std::wstring targetPath;   // What the shortcut launches (empty for shell/Store items)
    std::wstring iconPath;     // Icon location as stored in the .lnk (may contain %VARS%), empty = none
    int          iconIndex = 0;
    bool         onTaskbar = true; // false = leftover file from an earlier pin; editing it does nothing
    std::wstring aumid;        // AppUserModelID: set on shortcuts to packaged apps
    bool         packaged = false; // pinned Store/MSIX app with no .lnk (lnkPath empty)
};

/// %APPDATA%\Microsoft\Internet Explorer\Quick Launch\User Pinned\TaskBar
std::wstring GetPinnedShortcutsFolder();

/// Every readable .lnk in the pinned folder: live pins first, then by name.
/// Re-pinning an app makes Windows write "App (2).lnk" and orphan "App.lnk";
/// the Taskband registry blob tells us which file the taskbar really uses.
std::vector<PinnedShortcut> EnumeratePinnedShortcuts();

/// True if `lnkFileName` (no folder) appears in the Taskband "Favorites" blob.
bool BlobMentionsFile(const std::vector<unsigned char>& blob, const std::wstring& lnkFileName);

/// App IDs of packaged apps pinned directly ("Claude_pzs8sxrjxfjjc!Claude"): these pins
/// have no .lnk file, only an entry in the Taskband blob.
std::vector<std::wstring> ExtractPinnedAppIds(const std::vector<unsigned char>& blob);

/// "shell:AppsFolder\<aumid>" - the shell item for a packaged app.
std::wstring AppsFolderPath(const std::wstring& aumid);

/// The app's display name from the shell ("Claude"), or empty.
std::wstring AppDisplayName(const std::wstring& aumid);

/// Where Iconger keeps shortcuts it makes for packaged apps
/// (Start menu > Programs > Iconger, so Windows offers "Pin to taskbar" on them).
std::wstring AppShortcutsFolder();

/// Create a shortcut that launches the packaged app `aumid` with a custom icon. It carries
/// the same AppUserModelID, so the running window groups with it on the taskbar.
bool CreateAppShortcut(const std::wstring& lnkPath, const std::wstring& aumid,
                       const std::wstring& iconPath, int iconIndex);

/// Read one .lnk. Returns false if it cannot be loaded.
bool ReadShortcut(const std::wstring& lnkPath, PinnedShortcut& out);

/// Point the shortcut's icon at newIconPath,newIconIndex and save it.
/// An empty newIconPath removes the override so the target's own icon is used.
bool SetShortcutIcon(const std::wstring& lnkPath, const std::wstring& newIconPath, int newIconIndex);

/// The file the shortcut's icon currently comes from (expanded), plus its index.
/// Falls back to the target when the .lnk has no explicit icon location.
void ResolveShortcutIcon(const PinnedShortcut& sc, std::wstring& path, int& index);
