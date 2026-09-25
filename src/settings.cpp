#include "settings.h"
#include "app_paths.h"
#include <windows.h>
#include <iterator>

static std::wstring SettingsFile() { return DataDir() + L"\\settings.ini"; }

static bool ReadBool(const wchar_t* key, bool def)
{
    return GetPrivateProfileIntW(L"iconger", key, def ? 1 : 0, SettingsFile().c_str()) != 0;
}

static void WriteBool(const wchar_t* key, bool v)
{
    WritePrivateProfileStringW(L"iconger", key, v ? L"1" : L"0", SettingsFile().c_str());
}

void Settings::Load()
{
    clearIconCache    = ReadBool(L"clearIconCache", clearIconCache);
    confirmRestart    = ReadBool(L"confirmRestart", confirmRestart);
    onlineLibraries   = ReadBool(L"onlineLibraries", onlineLibraries);
    startMenuShortcut = ReadBool(L"startMenuShortcut", startMenuShortcut);
    checkUpdates      = ReadBool(L"checkUpdates", checkUpdates);
    // Versions before the welcome screen didn't write this; anyone with settings or
    // backups from them has used Iconger already.
    bool usedBefore = FileExists(SettingsFile()) || FileExists(DataDir() + L"\\backups.tsv");
    welcomed          = ReadBool(L"welcomed", usedBefore);
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(L"iconger", L"skippedVersion", L"", buf, (DWORD)std::size(buf), SettingsFile().c_str());
    skippedVersion = buf;
}

void Settings::Save() const
{
    WriteBool(L"clearIconCache", clearIconCache);
    WriteBool(L"confirmRestart", confirmRestart);
    WriteBool(L"onlineLibraries", onlineLibraries);
    WriteBool(L"startMenuShortcut", startMenuShortcut);
    WriteBool(L"checkUpdates", checkUpdates);
    WriteBool(L"welcomed", welcomed);
    WritePrivateProfileStringW(L"iconger", L"skippedVersion", skippedVersion.c_str(), SettingsFile().c_str());
}
