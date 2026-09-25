#include "ui/anim.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include <imgui_internal.h>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

namespace anim {

namespace {

struct SmoothState { float value; int lastFrame; };
std::unordered_map<ImGuiID, SmoothState> g_smooth;

std::string g_sceneKey;
double g_sceneStart = -100.0;

float Clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

// Deltas get large after the main loop idled; cap them so a glide isn't skipped.
float FrameDelta() { return std::min(ImGui::GetIO().DeltaTime, 1.0f / 30.0f); }

void Transform(ImDrawList* dl, int from, float alpha, ImVec2 offset, float scale, ImVec2 pivot, bool clipRects)
{
    if (!dl) return;
    const bool move = offset.x != 0 || offset.y != 0 || scale != 1.0f;
    auto tr = [&](ImVec2 p) {
        return ImVec2(pivot.x + (p.x - pivot.x) * scale + offset.x, pivot.y + (p.y - pivot.y) * scale + offset.y);
    };
    for (int i = from; i < dl->VtxBuffer.Size; ++i) {
        ImDrawVert& v = dl->VtxBuffer.Data[i];
        if (move) v.pos = tr(v.pos);
        if (alpha < 1.0f) {
            unsigned a = (v.col >> IM_COL32_A_SHIFT) & 0xFF;
            v.col = (v.col & ~IM_COL32_A_MASK) | ((ImU32)(a * alpha + 0.5f) << IM_COL32_A_SHIFT);
        }
    }
    // A child window's clip rectangle has to travel with its content, or the moved
    // content gets cut off at the old edges.
    if (clipRects && move) {
        for (ImDrawCmd& cmd : dl->CmdBuffer) {
            ImVec2 a = tr(ImVec2(cmd.ClipRect.x, cmd.ClipRect.y)), b = tr(ImVec2(cmd.ClipRect.z, cmd.ClipRect.w));
            cmd.ClipRect = ImVec4(a.x, a.y, b.x, b.y);
        }
        ImVec4& cr = dl->_CmdHeader.ClipRect;
        ImVec2 a = tr(ImVec2(cr.x, cr.y)), b = tr(ImVec2(cr.z, cr.w));
        cr = ImVec4(a.x, a.y, b.x, b.y);
    }
}

void TransformChildren(ImGuiWindow* w, int fromChild, float alpha, ImVec2 offset, float scale, ImVec2 pivot)
{
    const int frame = ImGui::GetFrameCount();
    for (int i = fromChild; i < w->DC.ChildWindows.Size; ++i) {
        ImGuiWindow* c = w->DC.ChildWindows[i];
        if (!c || !c->Active || c->LastFrameActive != frame) continue;
        Transform(c->DrawList, 0, alpha, offset, scale, pivot, true);
        TransformChildren(c, 0, alpha, offset, scale, pivot);
    }
}

} // namespace

float EaseOutCubic(float t)
{
    t = Clamp01(t);
    float u = 1 - t;
    return 1 - u * u * u;
}

float EaseOutBack(float t, float s)
{
    t = Clamp01(t) - 1;
    return t * t * ((s + 1) * t + s) + 1;
}

bool Enabled()
{
    // Re-read now and then: the user can flip it while Iconger runs.
    static bool enabled = true;
    static double checked = -10;
    double now = ImGui::GetTime();
    if (now - checked > 5.0) {
        BOOL on = TRUE;
        enabled = !SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0) || on;
        checked = now;
    }
    return enabled;
}

float Smooth(ImGuiID id, float target, float speed, float initial)
{
    const int frame = ImGui::GetFrameCount();
    auto it = g_smooth.find(id);
    if (it == g_smooth.end() || it->second.lastFrame < frame - 2) // new, or not drawn for a while
        it = g_smooth.insert_or_assign(id, SmoothState{ initial < 0 ? target : initial, frame }).first;
    SmoothState& s = it->second;
    if (s.lastFrame != frame) {
        s.lastFrame = frame;
        if (!Enabled()) s.value = target;
        else s.value += (target - s.value) * (1.0f - std::exp(-speed * FrameDelta()));
        if (std::fabs(target - s.value) < 0.002f) s.value = target;
        else ui::KeepAnimating(0.05f);
    }
    // forget ids that disappeared (e.g. tiles of an old search)
    if ((frame & 255) == 0)
        for (auto i = g_smooth.begin(); i != g_smooth.end();)
            i = i->second.lastFrame < frame - 60 ? g_smooth.erase(i) : std::next(i);
    return s.value;
}

float Smooth(const char* id, float target, float speed, float initial)
{
    return Smooth(ImGui::GetID(id), target, speed, initial);
}

void Scene(const char* key)
{
    if (g_sceneKey != key) {
        g_sceneKey = key;
        g_sceneStart = ImGui::GetTime();
    }
}

float SceneTime() { return (float)(ImGui::GetTime() - g_sceneStart); }

float EnterAt(double start, int n, float stagger, float duration)
{
    if (!Enabled()) return 1.0f;
    // long lists: later items share one slot so the last doesn't lag behind
    float delay = std::min(n, 10) * stagger;
    float t = Clamp01(((float)(ImGui::GetTime() - start) - delay) / duration);
    if (t < 1.0f) ui::KeepAnimating(0.05f);
    return EaseOutCubic(t);
}

float Enter(int n, float stagger, float duration) { return EnterAt(g_sceneStart, n, stagger, duration); }

Block Begin()
{
    Block b;
    b.window = ImGui::GetCurrentWindow();
    b.list = ImGui::GetWindowDrawList();
    b.vtx = b.list->VtxBuffer.Size;
    b.children = b.window->DC.ChildWindows.Size;
    return b;
}

void End(const Block& b, float alpha, ImVec2 offset, float scale, ImVec2 pivot)
{
    if (!b.list || (alpha >= 1.0f && offset.x == 0 && offset.y == 0 && scale == 1.0f)) return;
    Transform(b.list, b.vtx, alpha, offset, scale, pivot, false);
    TransformChildren(b.window, b.children, alpha, offset, scale, pivot);
}

Rise::Rise(int n, float distance) : m_block(Begin()), m_t(Enter(n)), m_distance(distance) {}
Rise::Rise(double start, int n, float distance) : m_block(Begin()), m_t(EnterAt(start, n)), m_distance(distance) {}

Rise::~Rise()
{
    End(m_block, m_t, ImVec2(0, (1.0f - m_t) * theme::S(m_distance)));
}

void TransformWindow(ImGuiWindow* window, float alpha, ImVec2 offset, float scale, ImVec2 pivot)
{
    if (!window) return;
    Transform(window->DrawList, 0, alpha, offset, scale, pivot, true);
    TransformChildren(window, 0, alpha, offset, scale, pivot);
}

} // namespace anim
