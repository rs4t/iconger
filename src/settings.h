#pragma once

/// User preferences, persisted to %LOCALAPPDATA%\Iconger\settings.ini.
struct Settings {
    bool restartAfterApply = false; // restart Explorer right after each change
    bool clearIconCache    = true;  // delete iconcache_*.db during restarts
    bool confirmRestart    = true;  // ask before restarting Explorer
    bool compactList       = false; // list rows instead of the card grid

    void Load();
    void Save() const;
};
