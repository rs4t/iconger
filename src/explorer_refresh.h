#pragma once

/// Nudge the shell to re-read icons without restarting anything.
/// Enough for Start menu / desktop, usually NOT enough for the taskbar.
void SignalIconChange();

struct ExplorerRestartResult {
    bool ok = false;           // Explorer is running again
    bool forced = false;       // Restart Manager failed, processes were terminated instead
    int  cacheFilesDeleted = 0;
};

/// Stop Explorer through the Restart Manager (graceful, reopens folder windows),
/// optionally delete the icon cache while it is down, then bring it back.
/// Blocks for a few seconds: call it from a worker thread.
ExplorerRestartResult RestartExplorer(bool clearIconCache);
