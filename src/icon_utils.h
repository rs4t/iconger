#pragma once
#include <string>
#include <vector>
#include <cstdint>

/// Straight-alpha RGBA8 pixels, top-down.
struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    bool empty() const { return w <= 0 || h <= 0; }
};

/// Number of icons in an .exe/.dll/.ico (0 if none).
int CountIcons(const std::wstring& path);

/// Extract icon #index from path at (about) size x size pixels.
bool LoadIconImage(const std::wstring& path, int index, int size, Image& out);

/// Ask the shell for an item's icon (works for Store apps / shell folders whose
/// shortcuts have no file-based icon). Goes through the shell icon cache.
bool LoadShellItemImage(const std::wstring& path, int size, Image& out);

/// Kinds of files the icon picker accepts.
enum class IconSourceKind { Unsupported, IcoFile, ImageFile, IconLibrary };
IconSourceKind ClassifyIconSource(const std::wstring& path);

/// Turn a user-chosen .ico/.png/.jpg/... into a stable .ico under IconsDir().
/// Images are padded to square and rendered at 16..256 px so the icon is crisp at
/// every taskbar scale. Returns the new path, or empty with `error` filled.
std::wstring ImportIconFile(const std::wstring& src, std::string& error);

/// Write a multi-resolution .ico from square RGBA images (sizes 1..256).
bool WriteIco(const std::wstring& path, const std::vector<Image>& images);

/// Pad to a centered square and resize. Exposed for tests.
Image MakeSquareResized(const Image& src, int size);
