#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Export / import of a whole setup: which app gets which icon, with the icons inside
// the file (so it works on another PC, where the icon files don't exist).
// Format: JSON, file extension .iconger.

struct SetupItem {
    std::wstring name;        // "Firefox" (what the list shows)
    std::wstring shortcut;    // pinned shortcut file name, "Firefox.lnk" (empty for unpinned apps)
    std::wstring program;     // file name of what it launches, "firefox.exe" (lower case)
    std::wstring programPath; // full path of the program (unpinned apps: where it was on the old PC)
    std::wstring aumid;       // app ID of a Store app's shortcut, if any
    bool unpinned = false;    // EXPERIMENTAL: a running app that isn't pinned
    std::vector<uint8_t> ico; // the icon, as a complete .ico file
};

/// JSON text of a setup.
std::string SerializeSetup(const std::vector<SetupItem>& items, const std::string& appVersion);

/// Read a setup; false with `error` if it isn't one (or is from a newer, incompatible format).
bool ParseSetup(const std::string& json, std::vector<SetupItem>& out, std::string& error);

/// The icon at path,index as a complete .ico file: an .ico is read as is, anything else
/// (an icon inside an .exe/.dll) is extracted at 16-256 px.
bool IcoFileBytes(const std::wstring& path, int index, std::vector<uint8_t>& out);

std::string Base64Encode(const std::vector<uint8_t>& data);
bool Base64Decode(const std::string& text, std::vector<uint8_t>& out);
