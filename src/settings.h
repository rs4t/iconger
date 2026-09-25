#pragma once
#include <string>

/// User preferences, persisted to %LOCALAPPDATA%\Iconger\settings.ini.
struct Settings {
    bool clearIconCache    = true;  // delete iconcache_*.db during restarts
    bool confirmRestart    = true;  // ask before restarting Explorer
    bool onlineLibraries   = true;  // search Dashboard Icons / Papirus / Simple Icons
    bool startMenuShortcut = true;  // keep Programs\Iconger.lnk, so Windows search finds Iconger
    bool checkUpdates      = true;  // ask GitHub for a newer release at startup
    bool welcomed          = false; // the first-run welcome screen was dismissed
    std::wstring skippedVersion;    // "Skip this version" in the update dialog: don't ask for it at startup
    std::wstring lastSeenVersion;   // "What's new" is shown once after every update
    bool unpinnedIcons     = false; // EXPERIMENTAL: custom icons for running apps that aren't pinned

    void Load();
    void Save() const;
};
