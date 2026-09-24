#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Small string/path helpers shared by the whole app.

std::string  WideToUtf8(const std::wstring& w);
std::wstring Utf8ToWide(const std::string& s);

/// Expand %VARS% (shortcut icon locations often contain them).
std::wstring ExpandEnv(const std::wstring& s);

/// Lower-cased extension including the dot (".png"), or empty.
std::wstring LowerExt(const std::wstring& path);

/// File name without directory or extension.
std::wstring FileStem(const std::wstring& path);

bool FileExists(const std::wstring& path);
bool ReadWholeFile(const std::wstring& path, std::vector<uint8_t>& out);

/// %LOCALAPPDATA%\Iconger, created on first use. Can be overridden for tests.
std::wstring DataDir();
void SetDataDirForTests(const std::wstring& dir);

/// %LOCALAPPDATA%\Iconger\icons - converted/copied icons live here so they
/// survive temp cleanup and deleted downloads.
std::wstring IconsDir();
