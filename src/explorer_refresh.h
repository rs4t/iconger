#pragma once
#include <windows.h>

/// Try a lightweight shell change notification to signal icon changes.
/// Returns true if the call succeeded (this is the non-nuclear option).
bool SignalIconChange();

/// Clear the Explorer icon cache by deleting iconcache_*.db files
/// in %LOCALAPPDATA%\Microsoft\Windows\Explorer\.
/// Returns true if any files were deleted successfully.
bool ClearIconCache();

/// Restart the Windows shell (Explorer) gracefully:
/// 1. Signal a shutdown via SHChangeNotify + WM_ENDSESSION approach
/// 2. Kill explorer.exe (it will auto-restart if "restart on crash" is on)
/// 3. If not auto-restarted, relaunch via ShellExecute("explorer.exe")
/// Returns true if the restart sequence was initiated.
bool RestartExplorer();

/// Perform the "nuclear" refresh: ClearIconCache() → RestartExplorer().
/// Shows standard OS UI briefly (desktop flicker, taskbar restart).
/// Returns true if the process was started.
bool NuclearRefresh();

/// Ask user (via a Windows MessageBox) if they want to do the nuclear refresh.
/// Returns true if the user clicked Yes.
bool ConfirmNuclearRefresh(HWND parentHwnd);