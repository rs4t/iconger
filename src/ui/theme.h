#pragma once
#include <imgui.h>

// Iconger's own look: warm "plum graphite" darks, one tangerine accent (the colour of
// the tile being swapped in the logo), cards with a hairline border, Lucide icons.
// Keep in sync with assets/make_icon.py.
namespace theme {

// Palette
inline constexpr ImU32 bg            = IM_COL32( 17,  15,  20, 255); // #110f14 window
inline constexpr ImU32 panel         = IM_COL32( 21,  18,  26, 255); // #15121a content area
inline constexpr ImU32 card          = IM_COL32( 27,  24,  33, 255); // #1b1821
inline constexpr ImU32 cardHover     = IM_COL32( 35,  31,  42, 255); // #231f2a
inline constexpr ImU32 border        = IM_COL32( 44,  39,  51, 255); // #2c2733
inline constexpr ImU32 borderStrong  = IM_COL32( 61,  54,  71, 255); // #3d3647
inline constexpr ImU32 accentBg      = IM_COL32( 42,  37,  49, 255); // #2a2531 hover / chips
inline constexpr ImU32 logoSlot      = IM_COL32( 74,  66,  85, 255); // #4a4255 logo slots
inline constexpr ImU32 primary       = IM_COL32(255, 138,  61, 255); // #ff8a3d tangerine
inline constexpr ImU32 primaryHover  = IM_COL32(255, 162,  95, 255); // #ffa25f
inline constexpr ImU32 primarySoft   = IM_COL32(255, 138,  61,  34);
inline constexpr ImU32 onPrimary     = IM_COL32( 33,  17,   6, 255); // text on tangerine
inline constexpr ImU32 success       = IM_COL32( 76, 195, 138, 255); // #4cc38a
inline constexpr ImU32 successSoft   = IM_COL32( 76, 195, 138,  34);
inline constexpr ImU32 warning       = IM_COL32(245, 196,  81, 255); // #f5c451
inline constexpr ImU32 warningSoft   = IM_COL32(245, 196,  81,  28);
inline constexpr ImU32 warningBorder = IM_COL32(245, 196,  81,  70);
inline constexpr ImU32 danger        = IM_COL32(239,  90, 111, 255); // #ef5a6f
inline constexpr ImU32 dangerSoft    = IM_COL32(239,  90, 111,  32);
inline constexpr ImU32 violet        = IM_COL32(167, 139, 250, 255); // #a78bfa
inline constexpr ImU32 violetSoft    = IM_COL32(167, 139, 250,  34);
inline constexpr ImU32 text          = IM_COL32(245, 241, 247, 255); // #f5f1f7
inline constexpr ImU32 textDim       = IM_COL32(205, 197, 211, 255); // #cdc5d3
inline constexpr ImU32 textSecondary = IM_COL32(150, 140, 159, 255); // #968c9f
inline constexpr ImU32 textMuted     = IM_COL32( 94,  86, 104, 255); // #5e5668

/// Same colour as bg, for Win32 (title bar, window brush, swap-chain clear).
inline constexpr unsigned long bgColorRef = 0x00140F11; // RGB(17, 15, 20) as COLORREF (0x00BBGGRR)

// Fonts (ImGui 1.92 atlas is dynamic, so one ImFont serves every size)
struct Fonts {
    ImFont* regular = nullptr;
    ImFont* semibold = nullptr;
    ImFont* bold = nullptr;
};
extern Fonts fonts;

inline constexpr float fontBody  = 15.0f;
inline constexpr float fontSmall = 13.0f;
inline constexpr float fontH2    = 17.0f;
inline constexpr float fontH1    = 24.0f;

/// Loads Segoe UI (+ Lucide icons, + CJK fallback when available).
void LoadFonts();

/// Rebuild the ImGui style for a DPI scale (1.0 = 96 dpi).
void ApplyStyle(float dpiScale);

/// Current DPI scale; use S() for every hard-coded pixel size.
float Scale();
inline float S(float px) { return px * Scale(); }

} // namespace theme

// Lucide glyphs (lucide-static 1.48.0 codepoints, see CMakeLists.txt)
#define ICON_PIN            "\uE259"
#define ICON_HISTORY        "\uE1F5"
#define ICON_SETTINGS       "\uE154"
#define ICON_REFRESH        "\uE145"
#define ICON_FOLDER_OPEN    "\uE247"
#define ICON_SEARCH         "\uE151"
#define ICON_CHECK          "\uE06C"
#define ICON_X              "\uE1B2"
#define ICON_ALERT          "\uE193"
#define ICON_INFO           "\uE0F9"
#define ICON_CIRCLE_CHECK   "\uE226"
#define ICON_CIRCLE_X       "\uE084"
#define ICON_IMAGE          "\uE0F6"
#define ICON_IMAGE_PLUS     "\uE1F7"
#define ICON_UNDO           "\uE2A1"
#define ICON_EXTERNAL       "\uE0B9"
#define ICON_UPLOAD         "\uE19E"
#define ICON_APP_WINDOW     "\uE426"
#define ICON_PACKAGE        "\uE129"
#define ICON_POWER          "\uE140"
#define ICON_LAYOUT_GRID    "\uE0FF"
#define ICON_CHEVRON_RIGHT  "\uE06F"
#define ICON_CODE           "\uE206"
#define ICON_ROTATE_CCW     "\uE148"
#define ICON_ARCHIVE        "\uE041"
#define ICON_MONITOR_COG    "\uE603"
#define ICON_ARROW_LEFT     "\uE048"
#define ICON_ARROW_RIGHT    "\uE049"
#define ICON_LOADER         "\uE10A"
#define ICON_FILE_IMAGE     "\uE31C"
#define ICON_TRASH          "\uE18E"
