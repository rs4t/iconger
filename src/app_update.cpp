// Self-update (check, install, sidebar status) and the first-run welcome screen.
#include "app_internal.h"

using namespace appui;
using Microsoft::WRL::ComPtr;

// ============================================================================
// Updates
// ============================================================================

void App::StartUpdateCheck(bool manual)
{
    if (m_update == UpdateState::Checking || m_update == UpdateState::Installing) return;
    m_update = UpdateState::Checking;
    m_updateTime = ImGui::GetTime();
    m_updateManual = manual;
    m_updateError.clear();
    m_jobs.Submit([this]() -> JobPool::Done {
        ReleaseInfo rel;
        std::string err;
        bool ok = FetchLatestRelease(rel, err);
        return [this, ok, rel, err] {
            m_updateTime = ImGui::GetTime();
            if (!ok) {
                m_update = UpdateState::Failed;
                m_updateError = err;
                if (m_updateManual) ui::Toast(ui::ToastKind::Error, "Couldn't check for updates: " + err);
            } else if (!IsNewerVersion(rel.version, ICONGER_VERSION)) {
                m_update = UpdateState::UpToDate;
                if (m_updateManual) ui::Toast(ui::ToastKind::Success, "You have the latest version (v" ICONGER_VERSION ").");
            } else {
                m_release = rel;
                m_update = UpdateState::Available;
                // a skipped version still shows in the sidebar, it just doesn't pop up by itself
                m_openUpdate = m_updateManual || Utf8ToWide(rel.version) != m_settings.skippedVersion;
            }
        };
    });
}

void App::InstallUpdate()
{
    if (m_update != UpdateState::Available) return;
    m_update = UpdateState::Installing;
    m_updateTime = ImGui::GetTime();
    m_updateError.clear();
    m_jobs.Submit([this, rel = m_release, exe = ExePath()]() -> JobPool::Done {
        std::string err;
        bool ok = DownloadAndInstallUpdate(rel, exe, err);
        return [this, ok, err] {
            m_updateTime = ImGui::GetTime();
            if (ok) {
                m_relaunch = true; // main closes the window and starts the new exe
                return;
            }
            m_update = UpdateState::Available;
            m_updateError = err;
            m_openUpdate = true;
        };
    });
}

// Small status line at the bottom of the sidebar: checking / up to date / update available.
void App::DrawUpdateStatus(float width)
{
    const double age = ImGui::GetTime() - m_updateTime;
    const bool transient = m_update == UpdateState::UpToDate || m_update == UpdateState::Failed;
    const double shownFor = 4.0;
    if (m_update == UpdateState::Idle || (transient && age > shownFor)) return;
    float alpha = std::min(std::clamp((float)age / 0.25f, 0.0f, 1.0f), // fade in
                           transient ? std::clamp((float)(shownFor - age) / 0.6f, 0.0f, 1.0f) : 1.0f);
    // redraw only while something moves: a spinner, the fade in, or a fade out
    const bool spinning = m_update == UpdateState::Checking || m_update == UpdateState::Installing;
    if (spinning || transient || age < 0.3) ui::KeepAnimating(0.1f);
    auto fade = [alpha](ImU32 c) {
        ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
        v.w *= alpha;
        return ImGui::ColorConvertFloat4ToU32(v);
    };

    ImGui::SetCursorPos(ImVec2(S(12), ImGui::GetWindowHeight() - S(88)));
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size(width - S(24), S(32));
    const bool clickable = m_update == UpdateState::Available;
    bool hovered = false;
    if (clickable) {
        if (ImGui::InvisibleButton("##update", size)) m_openUpdate = true;
        hovered = ImGui::IsItemHovered();
    } else {
        ImGui::Dummy(size);
    }
    if (m_update == UpdateState::Failed && ImGui::IsItemHovered()) ui::Tooltip(m_updateError.c_str());

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (clickable)
        dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), fade(hovered ? IM_COL32(255, 138, 61, 60) : primarySoft), S(8));

    const char* icon = nullptr; // nullptr = spinner
    ImU32 iconCol = textSecondary, textCol = textSecondary;
    std::string label;
    switch (m_update) {
    case UpdateState::Checking:   label = "Checking for updates"; break;
    case UpdateState::Installing: label = "Updating..."; break;
    case UpdateState::UpToDate:   icon = ICON_CIRCLE_CHECK; iconCol = success; label = "Up to date"; break;
    case UpdateState::Failed:     icon = ICON_ALERT; iconCol = textMuted; textCol = textMuted; label = "Couldn't check for updates"; break;
    case UpdateState::Available:  icon = ICON_DOWNLOAD; iconCol = primary; textCol = text; label = "Update to v" + m_release.version; break;
    default: return;
    }

    ImGui::PushFont(nullptr, fontSmall);
    const float cy = p.y + size.y * 0.5f;
    const float fh = ImGui::GetFontSize();
    if (icon) {
        dl->AddText(ImVec2(p.x + S(10), cy - fh * 0.5f), fade(iconCol), icon);
    } else {
        ImVec2 c(p.x + S(17), cy);
        float a0 = (float)ImGui::GetTime() * 6.0f;
        dl->PathArcTo(c, S(5.5f), a0, a0 + IM_PI * 1.4f, 24);
        dl->PathStroke(fade(textSecondary), S(1.6f));
    }
    ImGui::PushClipRect(p, ImVec2(p.x + size.x - S(6), p.y + size.y), true);
    dl->AddText(ImVec2(p.x + S(32), cy - fh * 0.5f), fade(textCol), label.c_str());
    ImGui::PopClipRect();
    ImGui::PopFont();
}

// ============================================================================
// First run
// ============================================================================

void App::DrawWelcome()
{
    anim::Scene("welcome");
    ImGui::SetCursorPos(ImVec2(0, 0));
    // scrolls (wheel only, no bar) if the window is too small for it
    ImGui::BeginChild("##welcome", ImVec2(0, 0), 0, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
    const float winW = ImGui::GetWindowWidth(), winH = ImGui::GetWindowHeight();
    const float colW = std::min(S(520), winW - S(48));
    const float x0 = (winW - colW) * 0.5f;
    const float top = std::max(S(20), (winH - m_welcomeHeight) * 0.5f);
    ImGui::SetCursorPos(ImVec2(x0, top));

    auto centered = [&](const char* s, ImU32 col) {
        ImGui::SetCursorPosX(x0 + std::max(0.0f, (colW - ImGui::CalcTextSize(s).x) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(s);
        ImGui::PopStyleColor();
    };

    ImGui::PushTextWrapPos(x0 + colW);
    {
        // the logo pops in, overshooting a little
        const float logo = S(64);
        ImGui::SetCursorPosX(x0 + (colW - logo) * 0.5f);
        ImVec2 lp = ImGui::GetCursorScreenPos();
        anim::Block b = anim::Begin();
        ui::Logo(logo);
        float t = anim::Enter(0, 0.0f, 0.55f);
        float s = anim::Enabled() ? 0.55f + 0.45f * anim::EaseOutBack(std::min(1.0f, anim::SceneTime() / 0.55f), 2.2f) : 1.0f;
        anim::End(b, t, ImVec2(0, 0), s, ImVec2(lp.x + logo * 0.5f, lp.y + logo * 0.5f));
    }
    ImGui::Dummy(ImVec2(0, S(4)));
    {
        anim::Rise r(2);
        ImGui::PushFont(fonts.bold, 28.0f);
        centered("Welcome to Iconger", text);
        ImGui::PopFont();
    }
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - S(4));
    {
        anim::Rise r(3);
        centered("Give the apps on your taskbar the icons you want.", textSecondary);
    }
    ImGui::Dummy(ImVec2(0, S(12)));

    struct Step { const char* icon; ImU32 col, soft; const char* title; const char* body; };
    const Step steps[] = {
        { ICON_PIN, primary, primarySoft, "Pick an app on your taskbar",
          "Everything you've pinned shows up, Store apps included." },
        { ICON_WAND, violet, violetSoft, "Choose its new icon",
          "About 20,000 ready-made icons, the app's own alternatives, or your own pictures. Tweak the colours if you like." },
        { ICON_UNDO, success, successSoft, "Undo any time",
          "The original icon is backed up first, and the Restore page puts it back." },
    };
    int n = 5;
    for (const Step& st : steps) {
        anim::Rise r(n++);
        ImGui::SetCursorPosX(x0);
        ui::IconTile(st.icon, st.col, st.soft, S(40));
        ImGui::SameLine(0, S(14));
        ImGui::BeginGroup();
        ui::Heading(st.title, st.body, fontBody + 1);
        ImGui::EndGroup();
        ImGui::Dummy(ImVec2(0, S(4)));
    }
    ImGui::PopTextWrapPos();

    {
        anim::Rise r(n++);
        ImGui::SetCursorPosX(x0);
        ui::BeginCard("##welcomeopts", ImVec2(colW, 0), 16);
        ui::SettingRow("Add Iconger to the Start menu",
                       "Find it by searching \"Iconger\" in Windows, like any other app.", &m_settings.startMenuShortcut);
        ui::SettingRow("Check for updates", "When Iconger opens, it looks for a newer version on GitHub and asks before installing it.",
                       &m_settings.checkUpdates);
        ui::EndCard();
    }

    ImGui::Dummy(ImVec2(0, S(6)));
    bool start = false;
    {
        anim::Rise r(n++);
        ImGui::SetCursorPosX(x0);
        start = ui::Button("Get started", ICON_ARROW_RIGHT, ui::ButtonKind::Primary, ImVec2(colW, S(42))) ||
                ImGui::IsKeyPressed(ImGuiKey_Enter);
        ImGui::PushFont(nullptr, fontSmall);
        ImGui::Dummy(ImVec2(0, S(2)));
        centered("You can change these later in Settings.", textMuted);
        ImGui::PopFont();
    }
    if (start) {
        m_settings.welcomed = true;
        m_settings.Save();
        if (m_settings.startMenuShortcut) EnsureStartMenuShortcut(ExePath());
        else RemoveStartMenuShortcut();
        if (m_settings.checkUpdates) StartUpdateCheck(false);
        m_startedAt = ImGui::GetTime(); // the main window's menu rises in now
    }

    // centre vertically next frame (the content's height is only known once drawn)
    float h = ImGui::GetCursorPosY() - top;
    if (std::fabs(h - m_welcomeHeight) > 1.0f) ui::KeepAnimating(0.1f);
    m_welcomeHeight = h;
    ImGui::Dummy(ImVec2(0, S(16)));
    ImGui::EndChild();
}
