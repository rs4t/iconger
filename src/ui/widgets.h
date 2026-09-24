#pragma once
#include <imgui.h>
#include <string>

// Small widget kit on top of Dear ImGui (cards, buttons, toggles, toasts).
namespace ui {

enum class ButtonKind { Primary, Secondary, Outline, Danger, Ghost };

/// Button with optional leading Lucide icon. size.x == 0 -> fit content, < 0 -> fill.
bool Button(const char* label, const char* icon = nullptr, ButtonKind kind = ButtonKind::Secondary,
            ImVec2 size = ImVec2(0, 0), bool enabled = true);

/// Square icon-only button with tooltip.
bool IconButton(const char* id, const char* icon, const char* tooltip, bool enabled = true);

/// The Iconger mark (same drawing as assets/make_icon.py), vector so it's crisp at any DPI.
/// withBackground=false draws just the tiles, for use on an existing surface.
void Logo(float size, bool withBackground = true);

/// Rounded square with a tinted background and a centred glyph (card headers).
void IconTile(const char* icon, ImU32 color, ImU32 softColor, float size);

/// Card container: rounded, bordered, padded. Height 0 = auto-fit contents.
bool BeginCard(const char* id, ImVec2 size = ImVec2(0, 0), float padding = 16.0f);
void EndCard();

/// Title + subtitle pair as used in card and page headers.
void Heading(const char* title, const char* subtitle = nullptr, float titleSize = 0);

/// Coloured pill label.
void Badge(const char* text, ImU32 color, ImU32 softColor);

/// iOS-style switch. Returns true when toggled.
bool Toggle(const char* id, bool* value);

/// Setting row: title/description on the left, toggle on the right.
bool SettingRow(const char* title, const char* description, bool* value);

enum class SliderTrack { Plain, Hue };

/// Labelled slider: name + value on one line, track below. Snaps to and
/// double-click resets to `def`. Values are whole numbers. Returns true when changed.
bool Slider(const char* label, float* v, float vmin, float vmax, float def, const char* fmt,
            SliderTrack track = SliderTrack::Plain);

/// Round colour swatch; returns true when clicked.
bool Swatch(const char* id, ImU32 color, bool selected, float size);

/// Text clipped with an ellipsis to maxWidth.
void TextEllipsis(const char* text, float maxWidth, ImU32 color);

/// Rotating loader glyph.
void Spinner(float size, ImU32 color);

/// Tooltip shown after a short hover.
void Tooltip(const char* text);

/// Ask the main loop to keep rendering for a while (animations).
void KeepAnimating(float seconds);
/// True while an animation or a toast is on screen.
bool IsAnimating();

// ---- toasts --------------------------------------------------------------
enum class ToastKind { Info, Success, Warning, Error };
void Toast(ToastKind kind, const std::string& message);
/// Draw toasts bottom-right. Returns true while any are visible (keep rendering).
bool RenderToasts();

} // namespace ui
