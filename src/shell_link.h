#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct PinnedShortcut {
    std::wstring filePath;         // Full path to the .lnk file
    std::wstring displayName;      // Friendly name (from IShellLinkW::GetDescription or filename)
    std::wstring targetPath;       // What the shortcut launches
    std::wstring iconPath;         // Current icon location
    int           iconIndex = 0;   // Current icon index
    HICON         hIcon = nullptr; // Extracted icon (32x32)
    bool          valid = true;    // false if the .lnk is malformed
};

/// Enumerate all .lnk files in the taskbar pinned shortcuts folder.
/// Returns a vector of PinnedShortcut structs, one per valid .lnk found.
std::vector<PinnedShortcut> EnumeratePinnedShortcuts();

/// Read a single .lnk file from disk and populate a PinnedShortcut.
/// Sets valid=false on failure (malformed .lnk, access denied, etc.).
PinnedShortcut ReadShortcut(const std::wstring& lnkPath);

/// Set the icon location (path + index) on an existing .lnk file and persist it.
/// Returns true on success.
bool SetShortcutIcon(const std::wstring& lnkPath,
                     const std::wstring& newIconPath,
                     int newIconIndex);

/// Get the pinned shortcuts folder path (as a wide string).
std::wstring GetPinnedShortcutsFolder();

/// Reset the icon location on a shortcut so it falls back to the target's
/// default embedded icon. Equivalent to clearing the IconLocation override.
/// Returns true on success.
bool ResetShortcutIcon(const std::wstring& lnkPath);