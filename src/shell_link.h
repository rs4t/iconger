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
};

/// %APPDATA%\Microsoft\Internet Explorer\Quick Launch\User Pinned\TaskBar
std::wstring GetPinnedShortcutsFolder();

/// Every readable .lnk in the pinned folder: live pins first, then by name.
/// Re-pinning an app makes Windows write "App (2).lnk" and orphan "App.lnk";
/// the Taskband registry blob tells us which file the taskbar really uses.
std::vector<PinnedShortcut> EnumeratePinnedShortcuts();

/// True if `lnkFileName` (no folder) appears in the Taskband "Favorites" blob.
bool BlobMentionsFile(const std::vector<unsigned char>& blob, const std::wstring& lnkFileName);

/// Read one .lnk. Returns false if it cannot be loaded.
bool ReadShortcut(const std::wstring& lnkPath, PinnedShortcut& out);

/// Point the shortcut's icon at newIconPath,newIconIndex and save it.
/// An empty newIconPath removes the override so the target's own icon is used.
bool SetShortcutIcon(const std::wstring& lnkPath, const std::wstring& newIconPath, int newIconIndex);

/// The file the shortcut's icon currently comes from (expanded), plus its index.
/// Falls back to the target when the .lnk has no explicit icon location.
void ResolveShortcutIcon(const PinnedShortcut& sc, std::wstring& path, int& index);
