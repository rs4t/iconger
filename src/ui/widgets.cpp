#include "ui/widgets.h"
#include "ui/theme.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace theme;

namespace ui {

static double g_animateUntil = 0;
static constexpr double kToastLife = 4.0;

void KeepAnimating(float seconds)
{
    g_animateUntil = std::max(g_animateUntil, ImGui::GetTime() + seconds);
}

static ImU32 Fade(ImU32 c, float a)
{
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.w *= a;
    return ImGui::ColorConvertFloat4ToU32(v);
}

bool Button(const char* label, const char* icon, ButtonKind kind, ImVec2 size, bool enabled)
{
    ImGuiStyle& st = ImGui::GetStyle();
    const bool hasLabel = label && *label && label[0] != '#';
    const char* labelEnd = label ? ImGui::FindRenderedTextEnd(label) : nullptr;
    ImVec2 labelSize = hasLabel ? ImGui::CalcTextSize(label, labelEnd) : ImVec2(0, 0);
    float iconW = icon ? ImGui::CalcTextSize(icon).x : 0;
    float gap = icon && hasLabel ? S(8) : 0;
    float contentW = iconW + gap + labelSize.x;

    if (size.x == 0) size.x = contentW + st.FramePadding.x * 2;
    if (size.x < 0) size.x = ImGui::GetContentRegionAvail().x;
    if (size.y == 0) size.y = ImGui::GetFontSize() + st.FramePadding.y * 2;

    ImGui::PushID(label ? label : icon);
    ImGui::BeginDisabled(!enabled);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton("##btn", size);
    bool hovered = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();
    ImGui::EndDisabled();
    ImGui::PopID();

    ImU32 fill = 0, line = 0, fg = text;
    switch (kind) {
    case ButtonKind::Primary:   fill = hovered ? primaryHover : primary; fg = onPrimary; break;
    case ButtonKind::Secondary: fill = hovered ? accentBg : card; line = border; break;
    case ButtonKind::Outline:   fill = hovered ? primarySoft : 0; line = primary; fg = primary; break;
    case ButtonKind::Danger:    fill = hovered ? danger : dangerSoft; line = danger; fg = hovered ? IM_COL32_WHITE : danger; break;
    case ButtonKind::Ghost:     fill = hovered ? accentBg : 0; fg = hovered ? text : textSecondary; break;
    }
    if (held) fill = Fade(fill ? fill : accentBg, 0.8f);
    float alpha = enabled ? 1.0f : 0.4f;
    if (!enabled && kind == ButtonKind::Primary) {
        // a faded tangerine reads as muddy brown; show a neutral inactive button instead
        fill = accentBg;
        fg = textMuted;
        alpha = 1.0f;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 max(pos.x + size.x, pos.y + size.y);
    if (fill) dl->AddRectFilled(pos, max, Fade(fill, alpha), st.FrameRounding);
    if (line) dl->AddRect(pos, max, Fade(line, alpha), st.FrameRounding, S(1));

    float x = pos.x + (size.x - contentW) * 0.5f;
    float y = pos.y + (size.y - ImGui::GetFontSize()) * 0.5f;
    if (icon) {
        dl->AddText(ImVec2(x, y), Fade(fg, alpha), icon);
        x += iconW + gap;
    }
    if (hasLabel) dl->AddText(ImVec2(x, y), Fade(fg, alpha), label, labelEnd);
    return pressed && enabled;
}

bool IconButton(const char* id, const char* icon, const char* tooltip, bool enabled)
{
    float side = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2;
    ImGui::PushID(id);
    bool pressed = Button("##icon", icon, ButtonKind::Secondary, ImVec2(side, side), enabled);
    ImGui::PopID();
    if (tooltip) Tooltip(tooltip);
    return pressed;
}

// Rounded square centred on c, rotated by `angle` radians, as a filled/stroked path.
static void RoundedSquarePath(ImDrawList* dl, ImVec2 c, float side, float radius, float angle)
{
    const float h = side * 0.5f - radius;
    const float ca = std::cos(angle), sa = std::sin(angle);
    const ImVec2 corners[4] = { { h, -h }, { h, h }, { -h, h }, { -h, -h } };
    for (int i = 0; i < 4; ++i) {
        float start = -IM_PI * 0.5f + IM_PI * 0.5f * i;
        for (int k = 0; k <= 6; ++k) {
            float t = start + IM_PI * 0.5f * k / 6;
            float x = corners[i].x + radius * std::cos(t), y = corners[i].y + radius * std::sin(t);
            dl->PathLineTo(ImVec2(c.x + x * ca - y * sa, c.y + x * sa + y * ca));
        }
    }
}

void Logo(float size, bool withBackground)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float u = size / 1024.0f; // coordinates below match make_icon.py's 1024 canvas
    auto P = [&](float x, float y) { return ImVec2(p.x + x * u, p.y + y * u); };

    if (withBackground) {
        dl->AddRectFilled(P(40, 40), P(984, 984), card, 230 * u);
        dl->AddRect(P(40, 40), P(984, 984), border, 230 * u, std::max(1.0f, 20 * u));
    }
    const float t = 250, gap = 64, x0 = (1024 - (t * 2 + gap)) / 2;
    for (int i = 0; i < 4; ++i) {
        ImVec2 c = P(x0 + t / 2 + (i % 2) * (t + gap), x0 + t / 2 + (i / 2) * (t + gap));
        RoundedSquarePath(dl, c, i == 1 ? (t - 20) * u : t * u, (i == 1 ? 56 : 64) * u, 0);
        if (i == 1) dl->PathStroke(logoSlot, std::max(1.0f, 20 * u), ImDrawFlags_Closed); // the empty slot
        else dl->PathFillConvex(logoSlot);
    }
    ImVec2 slot = P(x0 + t * 1.5f + gap, x0 + t / 2);
    RoundedSquarePath(dl, ImVec2(slot.x + 40 * u, slot.y - 64 * u), (t + 30) * u, 70 * u, 14.0f * IM_PI / 180.0f);
    dl->PathFillConvex(primary);
}

void IconTile(const char* icon, ImU32 color, ImU32 softColor, float size)
{
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), softColor, S(10));
    float fs = size * 0.46f;
    ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0, icon);
    dl->AddText(ImGui::GetFont(), fs, ImVec2(pos.x + (size - ts.x) * 0.5f, pos.y + (size - ts.y) * 0.5f), color, icon);
}

bool BeginCard(const char* id, ImVec2 size, float padding)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, card);
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(padding), S(padding)));
    ImGuiChildFlags flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
    if (size.y == 0) flags |= ImGuiChildFlags_AutoResizeY;
    bool open = ImGui::BeginChild(id, size, flags, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    return open;
}

void EndCard() { ImGui::EndChild(); }

void Heading(const char* title, const char* subtitle, float titleSize)
{
    ImGui::PushFont(fonts.semibold, titleSize > 0 ? titleSize : fontH2);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    if (subtitle) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - S(8));
        ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
        ImGui::TextWrapped("%s", subtitle);
        ImGui::PopStyleColor();
    }
}

void Badge(const char* label, ImU32 color, ImU32 softColor)
{
    ImGui::PushFont(fonts.semibold, fontSmall - 1);
    ImVec2 ts = ImGui::CalcTextSize(label);
    ImVec2 pad(S(8), S(3));
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(ts.x + pad.x * 2, ts.y + pad.y * 2);
    ImGui::Dummy(size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), softColor, size.y * 0.5f);
    dl->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), color, label);
    ImGui::PopFont();
}

bool Toggle(const char* id, bool* value)
{
    float h = S(22), w = S(40);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    bool pressed = ImGui::InvisibleButton("##toggle", ImVec2(w, h));
    ImGui::PopID();
    if (pressed) { *value = !*value; KeepAnimating(0.4f); }

    // animate the knob
    ImGuiID key = ImGui::GetItemID();
    ImGuiStorage* store = ImGui::GetStateStorage();
    float t = store->GetFloat(key, *value ? 1.0f : 0.0f);
    float target = *value ? 1.0f : 0.0f;
    t += (target - t) * std::min(1.0f, ImGui::GetIO().DeltaTime * 18.0f);
    if (std::fabs(target - t) < 0.01f) t = target;
    store->SetFloat(key, t);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 track = ImGui::ColorConvertFloat4ToU32(ImLerp(
        ImGui::ColorConvertU32ToFloat4(accentBg), ImGui::ColorConvertU32ToFloat4(primary), t));
    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), track, h * 0.5f);
    float r = h * 0.5f - S(3);
    dl->AddCircleFilled(ImVec2(pos.x + h * 0.5f + t * (w - h), pos.y + h * 0.5f), r, IM_COL32_WHITE);
    return pressed;
}

bool SettingRow(const char* title, const char* description, bool* value)
{
    ImGui::PushID(title);
    float avail = ImGui::GetContentRegionAvail().x;
    float toggleW = S(40);
    ImVec2 start = ImGui::GetCursorPos();

    ImGui::PushTextWrapPos(start.x + avail - toggleW - S(24));
    ImGui::PushFont(fonts.semibold, 0);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - S(6));
    ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
    ImGui::TextWrapped("%s", description);
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImVec2 end = ImGui::GetCursorPos();

    float midY = start.y + (end.y - start.y - ImGui::GetStyle().ItemSpacing.y - S(22)) * 0.5f;
    ImGui::SetCursorPos(ImVec2(start.x + avail - toggleW, midY));
    bool changed = Toggle("##t", value);
    ImGui::SetCursorPos(end);
    ImGui::Dummy(ImVec2(0, 0)); // SetCursorPos alone doesn't extend the parent's bounds
    ImGui::PopID();
    return changed;
}

bool Slider(const char* label, float* v, float vmin, float vmax, float def, const char* fmt, SliderTrack track)
{
    ImGui::PushID(label);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;

    // label ........ value
    ImVec2 lp = ImGui::GetCursorScreenPos();
    char value[32];
    snprintf(value, sizeof(value), fmt, *v);
    ImVec2 vs = ImGui::CalcTextSize(value);
    bool isDefault = *v == def;
    dl->AddText(lp, textSecondary, label);
    dl->AddText(ImVec2(lp.x + w - vs.x, lp.y), isDefault ? textMuted : text, value);
    ImGui::Dummy(ImVec2(w, ImGui::GetFontSize()));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - S(6));

    // track
    const float h = S(22), knob = S(8), th = S(6);
    ImVec2 tp = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##slider", ImVec2(w, h));
    bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    const float x0 = tp.x + knob, x1 = tp.x + w - knob, cy = tp.y + h * 0.5f;
    auto toX = [&](float val) { return x0 + (val - vmin) / (vmax - vmin) * (x1 - x0); };

    bool changed = false;
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        changed = *v != def;
        *v = def;
    } else if (active) {
        float t = std::clamp((ImGui::GetIO().MousePos.x - x0) / (x1 - x0), 0.0f, 1.0f);
        float nv = std::round(vmin + t * (vmax - vmin));
        if (std::fabs(nv - def) <= (vmax - vmin) * 0.015f) nv = def; // sticky default
        if (nv != *v) { *v = nv; changed = true; }
    }
    if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    if (track == SliderTrack::Hue) {
        // rainbow: the knob shows how far colours rotate
        const ImU32 stops[7] = { IM_COL32(255, 0, 0, 255), IM_COL32(255, 255, 0, 255), IM_COL32(0, 255, 0, 255),
                                 IM_COL32(0, 255, 255, 255), IM_COL32(0, 0, 255, 255), IM_COL32(255, 0, 255, 255),
                                 IM_COL32(255, 0, 0, 255) };
        float seg = (x1 - x0) / 6;
        for (int i = 0; i < 6; ++i)
            dl->AddRectFilledMultiColor(ImVec2(x0 + seg * i, cy - th * 0.5f), ImVec2(x0 + seg * (i + 1), cy + th * 0.5f),
                                        stops[i], stops[i + 1], stops[i + 1], stops[i]);
        dl->AddCircleFilled(ImVec2(x0, cy), th * 0.5f, stops[0]);
        dl->AddCircleFilled(ImVec2(x1, cy), th * 0.5f, stops[6]);
    } else {
        dl->AddRectFilled(ImVec2(x0 - th * 0.5f, cy - th * 0.5f), ImVec2(x1 + th * 0.5f, cy + th * 0.5f), accentBg, th);
        float a = toX(def), b = toX(*v);
        if (a != b) dl->AddRectFilled(ImVec2(std::min(a, b), cy - th * 0.5f), ImVec2(std::max(a, b), cy + th * 0.5f), primary, th);
        dl->AddCircleFilled(ImVec2(a, cy), S(2), isDefault ? textMuted : primary); // tick at the default
    }
    float r = knob * (active ? 1.1f : hovered ? 1.05f : 1.0f);
    dl->AddCircleFilled(ImVec2(toX(*v), cy + S(1)), r, IM_COL32(0, 0, 0, 90));
    dl->AddCircleFilled(ImVec2(toX(*v), cy), r, IM_COL32_WHITE);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !active) ImGui::SetTooltip("Double-click to reset");
    ImGui::PopID();
    if (changed) KeepAnimating(0.2f);
    return changed;
}

bool Swatch(const char* id, ImU32 color, bool selected, float size)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
    bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + size * 0.5f, p.y + size * 0.5f);
    if (selected) dl->AddCircle(c, size * 0.5f - S(1), text, 0, S(2));
    else if (hovered) dl->AddCircle(c, size * 0.5f - S(1), borderStrong, 0, S(2));
    dl->AddCircleFilled(c, size * 0.5f - S(4), color);
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return pressed;
}

void TextEllipsis(const char* str, float maxWidth, ImU32 color)
{
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float h = ImGui::GetFontSize();
    ImGui::Dummy(ImVec2(std::min(maxWidth, ImGui::CalcTextSize(str).x), h));
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), pos, ImVec2(pos.x + maxWidth, pos.y + h),
                              pos.x + maxWidth, str, nullptr, nullptr);
    ImGui::PopStyleColor();
    bool clipped = ImGui::CalcTextSize(str).x > maxWidth;
    if (clipped && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", str);
}

void Spinner(float size, ImU32 color)
{
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(pos.x + size * 0.5f, pos.y + size * 0.5f);
    float r = size * 0.4f;
    float a0 = (float)ImGui::GetTime() * 6.0f;
    dl->PathArcTo(c, r, a0, a0 + IM_PI * 1.4f, 32);
    dl->PathStroke(color, size * 0.1f);
    KeepAnimating(0.1f);
}

bool IsAnimating() { return ImGui::GetTime() < g_animateUntil; }

void Tooltip(const char* str)
{
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(10), S(6)));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, S(8));
    ImGui::SetTooltip("%s", str);
    ImGui::PopStyleVar(2);
}

// ---- toasts --------------------------------------------------------------

struct ToastItem {
    ToastKind kind;
    std::string message;
    double born;
};
static std::vector<ToastItem> g_toasts;

void Toast(ToastKind kind, const std::string& message)
{
    g_toasts.push_back({ kind, message, ImGui::GetTime() });
    KeepAnimating((float)kToastLife + 0.1f);
    if (g_toasts.size() > 4) g_toasts.erase(g_toasts.begin());
}

bool RenderToasts()
{
    double now = ImGui::GetTime();
    g_toasts.erase(std::remove_if(g_toasts.begin(), g_toasts.end(),
        [now](const ToastItem& t) { return now - t.born > kToastLife; }), g_toasts.end());
    if (g_toasts.empty()) return false;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 display = ImGui::GetIO().DisplaySize;
    float width = S(340), margin = S(20), pad = S(14);
    float y = display.y - margin;

    for (auto it = g_toasts.rbegin(); it != g_toasts.rend(); ++it) {
        double age = now - it->born;
        float in = (float)std::min(1.0, age / 0.18);
        float out = (float)std::min(1.0, (kToastLife - age) / 0.3);
        float a = std::min(in, out);

        const char* icon = ICON_INFO;
        ImU32 col = primary;
        switch (it->kind) {
        case ToastKind::Success: icon = ICON_CIRCLE_CHECK; col = success; break;
        case ToastKind::Warning: icon = ICON_ALERT; col = warning; break;
        case ToastKind::Error:   icon = ICON_CIRCLE_X; col = danger; break;
        default: break;
        }

        float fs = ImGui::GetFontSize();
        float textW = width - pad * 3 - fs;
        ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, textW, it->message.c_str());
        float h = std::max(ts.y, fs) + pad * 2;
        float slide = (1.0f - in) * S(16);
        ImVec2 p0(display.x - margin - width + slide, y - h);
        ImVec2 p1(p0.x + width, y);

        dl->AddRectFilled(ImVec2(p0.x + S(2), p0.y + S(4)), ImVec2(p1.x + S(2), p1.y + S(4)), Fade(IM_COL32(0, 0, 0, 90), a), S(12));
        dl->AddRectFilled(p0, p1, Fade(card, a), S(12));
        dl->AddRect(p0, p1, Fade(border, a), S(12), S(1));
        dl->AddRectFilled(ImVec2(p0.x, p0.y + S(10)), ImVec2(p0.x + S(3), p1.y - S(10)), Fade(col, a), S(2));
        dl->AddText(ImVec2(p0.x + pad, p0.y + pad), Fade(col, a), icon);
        dl->AddText(ImGui::GetFont(), fs, ImVec2(p0.x + pad * 2 + fs, p0.y + pad), Fade(text, a),
                    it->message.c_str(), nullptr, textW);
        y -= h + S(10);
    }
    return true;
}

} // namespace ui
