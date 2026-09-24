#include "settings.h"
#include "app_paths.h"
#include <windows.h>

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
    restartAfterApply = ReadBool(L"restartAfterApply", restartAfterApply);
    clearIconCache    = ReadBool(L"clearIconCache", clearIconCache);
    confirmRestart    = ReadBool(L"confirmRestart", confirmRestart);
    compactList       = ReadBool(L"compactList", compactList);
}

void Settings::Save() const
{
    WriteBool(L"restartAfterApply", restartAfterApply);
    WriteBool(L"clearIconCache", clearIconCache);
    WriteBool(L"confirmRestart", confirmRestart);
    WriteBool(L"compactList", compactList);
}
