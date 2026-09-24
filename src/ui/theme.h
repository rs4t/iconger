#pragma once
#include <imgui.h>

// Visual language borrowed from Sparkle (github.com/thedogecraft/sparkle):
// deep navy background, slightly lighter cards with a 1px border, one blue accent,
// Lucide line icons, generous rounding.
namespace theme {

// Palette (Sparkle dark)
inline constexpr ImU32 bg            = IM_COL32( 12,  18,  31, 255); // #0c121f
inline constexpr ImU32 card          = IM_COL32( 19,  28,  44, 255); // #131c2c
inline constexpr ImU32 cardHover     = IM_COL32( 26,  37,  56, 255);
inline constexpr ImU32 border        = IM_COL32( 31,  42,  61, 255); // #1f2a3d
inline constexpr ImU32 borderStrong  = IM_COL32( 45,  60,  84, 255);
inline constexpr ImU32 accentBg      = IM_COL32( 36,  49,  68, 255); // #243144
inline constexpr ImU32 primary       = IM_COL32( 79, 144, 230, 255); // #4f90e6
inline constexpr ImU32 primaryHover  = IM_COL32(101, 160, 238, 255);
inline constexpr ImU32 primarySoft   = IM_COL32( 79, 144, 230,  38);
inline constexpr ImU32 success       = IM_COL32( 61, 181, 138, 255); // #3db58a
inline constexpr ImU32 successSoft   = IM_COL32( 61, 181, 138,  36);
inline constexpr ImU32 warning       = IM_COL32(234, 170,  64, 255);
inline constexpr ImU32 warningSoft   = IM_COL32(234, 170,  64,  30);
inline constexpr ImU32 danger        = IM_COL32(229,  83,  83, 255);
inline constexpr ImU32 dangerSoft    = IM_COL32(229,  83,  83,  32);
inline constexpr ImU32 purple        = IM_COL32(168, 110, 240, 255);
inline constexpr ImU32 purpleSoft    = IM_COL32(168, 110, 240,  36);
inline constexpr ImU32 text          = IM_COL32(240, 244, 248, 255); // #f0f4f8
inline constexpr ImU32 textDim       = IM_COL32(170, 180, 195, 255); // #aab4c3
inline constexpr ImU32 textSecondary = IM_COL32(126, 146, 169, 255); // #7e92a9
inline constexpr ImU32 textMuted     = IM_COL32( 75,  89, 112, 255); // #4b5970

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
#define ICON_SPARKLES       "\uE412"
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
