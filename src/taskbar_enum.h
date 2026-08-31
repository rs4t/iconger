#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct TaskbarButton {
    std::wstring exePath;        // Full path to the .exe behind the button
    std::wstring displayName;    // Window title or exe filename
    std::wstring lnkPath;        // Matching .lnk path if one was found in pinned folder
    HICON         hIcon = nullptr; // The icon actually displayed on the taskbar button
    HWND          buttonHwnd = nullptr; // The taskbar button window handle
    bool          hasEditableLnk = false; // true if lnkPath is valid and we can edit it
};

/// Enumerate the live taskbar buttons and collect info about each.
/// Tries to match each button to a shortcut in the old pinned shortcuts folder.
std::vector<TaskbarButton> EnumerateTaskbarButtons();