#pragma once

/// User preferences, persisted to %LOCALAPPDATA%\Iconger\settings.ini.
struct Settings {
    bool clearIconCache    = true;  // delete iconcache_*.db during restarts
    bool confirmRestart    = true;  // ask before restarting Explorer
    bool onlineLibraries   = true;  // search Dashboard Icons / Papirus / Simple Icons

    void Load();
    void Save() const;
};
