#pragma once
#include <imgui.h>

// Motion for an immediate-mode UI. Nothing here changes layout or hit-testing:
// blocks are drawn where they belong and their finished vertices are then faded,
// moved or scaled, so a click always lands where the item really is.
// Respects Windows' "Animation effects" setting (off = everything shows at once).
struct ImGuiWindow;

namespace anim {

float EaseOutCubic(float t);
float EaseOutBack(float t, float overshoot = 1.4f);

/// False when the user turned animations off in Windows.
bool Enabled();

/// A value that glides towards `target` (hover fades, sliding highlights, fade-ins).
/// `speed` is roughly 1/seconds. `initial` < 0 means "start at the target".
float Smooth(ImGuiID id, float target, float speed = 14.0f, float initial = -1.0f);
float Smooth(const char* id, float target, float speed = 14.0f, float initial = -1.0f);

// ---- entrance timelines -------------------------------------------------------
/// Start (or keep) a timeline. Changing `key` replays every entrance on the page.
void Scene(const char* key);
/// Entrance progress 0..1 of the n-th element of the current scene (staggered).
float Enter(int n, float stagger = 0.045f, float duration = 0.38f);
/// Same, for a timeline that started at `start` (ImGui::GetTime()), e.g. "an icon was picked".
float EnterAt(double start, int n, float stagger = 0.04f, float duration = 0.34f);
/// Seconds since the current scene started.
float SceneTime();

// ---- transforming a drawn block --------------------------------------------------------
struct Block {
    ImDrawList* list = nullptr;
    int vtx = 0;
    int children = 0;       // child windows of the current window at Begin
    ImGuiWindow* window = nullptr;
};

/// Remember where the current window's drawing is. Everything drawn until End()
/// (including child windows, e.g. cards) can then be transformed.
Block Begin();
/// Fade by `alpha`, move by `offset`, scale by `scale` around `pivot` (screen space).
void End(const Block& b, float alpha, ImVec2 offset = ImVec2(0, 0), float scale = 1.0f,
         ImVec2 pivot = ImVec2(0, 0));

/// Staggered "rise and fade in" of one element of the current scene.
struct Rise {
    explicit Rise(int n, float distance = 14.0f);
    /// On a timeline of its own that started at `start`.
    Rise(double start, int n, float distance = 10.0f);
    ~Rise();
    Rise(const Rise&) = delete;
    Rise& operator=(const Rise&) = delete;
private:
    Block m_block;
    float m_t;
    float m_distance;
};

/// Transform a whole window's drawing (and its child windows) - popups, pages.
void TransformWindow(ImGuiWindow* window, float alpha, ImVec2 offset, float scale, ImVec2 pivot);

} // namespace anim
