#pragma once
#include "icon_utils.h"
#include <cstdint>
#include <string>
#include <vector>

// Free online icon libraries, searched by app name and downloaded on demand.
// Everything is cached in %LOCALAPPDATA%\Iconger\cache, so each file is fetched once.
// All functions here block: call them from worker threads.

// Order = preference when matches score the same.
enum class IconLibrary {
    Dashboard,   // homarr-labs/dashboard-icons: full-colour app logos, PNG (Apache-2.0)
    WhiteSur,    // WhiteSur icon theme: macOS Big Sur style, SVG (GPL-3.0)
    Fluent,      // Fluent icon theme: Windows 11 style, SVG (GPL-3.0)
    Papirus,     // Papirus icon theme: flat desktop-app icons, SVG (GPL-3.0)
    Tela,        // Tela icon theme: flat, rounded, SVG (GPL-3.0)
    Candy,       // Candy icons: neon gradients, SVG (GPL-3.0)
    SimpleIcons, // simple-icons: brand marks, drawn white on a brand-coloured tile (CC0)
    Count
};
const char* LibraryName(IconLibrary lib);

struct LibraryIcon {
    IconLibrary lib = IconLibrary::Dashboard;
    std::string name;       // file name without extension / slug
    std::string title;      // human name (Simple Icons), else empty
    uint32_t brandRgb = 0;  // 0xRRGGBB, Simple Icons only
    std::vector<std::string> aliases;

    std::string Url() const;
    /// Unique, file-name safe id ("papirus-firefox").
    std::string Id() const;
};

// ---- network --------------------------------------------------------------
bool HttpGet(const std::string& url, std::vector<uint8_t>& out, std::string& error);
/// Cached GET. maxAgeHours < 0 = keep forever. A stale copy is used if the network fails.
bool CachedGet(const std::string& url, int maxAgeHours, std::vector<uint8_t>& out, std::string& error);

// ---- index + search -----------------------------------------------------------
class IconIndex {
public:
    /// Downloads (or reads cached) indexes of all libraries. Succeeds if at least one loads.
    bool Load(std::string& error);
    bool Empty() const { return m_icons.empty(); }
    size_t Size() const { return m_icons.size(); }

    /// Best matches for any of the queries (app name, exe name, search text), best first,
    /// at most perLibrary from any one library so every style gets a look-in.
    std::vector<LibraryIcon> Search(const std::vector<std::string>& queries, size_t limit, size_t perLibrary = 6) const;

    // Parsers, public for tests.
    static std::vector<LibraryIcon> ParseDashboardTree(const std::string& json);
    static std::vector<LibraryIcon> ParseSimpleIcons(const std::string& json);
    /// GitHub "git/trees" listing of an icon theme's apps folder.
    static std::vector<LibraryIcon> ParseGitHubTree(const std::string& json, IconLibrary lib);
    void AddForTests(std::vector<LibraryIcon> icons);

private:
    std::vector<LibraryIcon> m_icons;
};

/// Lower-case letters and digits only: "Visual Studio Code" -> "visualstudiocode".
std::string NormalizeName(const std::string& s);

// ---- rendering ------------------------------------------------------------------
/// Download (cached) and render at size x size.
bool FetchLibraryIcon(const LibraryIcon& icon, int size, Image& out, std::string& error);

/// Rasterise SVG text centred in a size x size transparent square.
/// forceColor != 0 paints every shape in that colour (0xAABBGGRR).
bool RenderSvg(const std::string& svg, int size, Image& out, uint32_t forceColor = 0);

/// Rounded tile in brand colour with the glyph centred on it (Simple Icons style).
Image MakeBrandTile(const std::string& glyphSvg, uint32_t brandRgb, int size);
