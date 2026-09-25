// The window frame: per-frame loop, keyboard shortcuts, sidebar and modal dialogs.
#include "app_internal.h"

using namespace appui;
using Microsoft::WRL::ComPtr;

// ============================================================================
// Frame
// ============================================================================

void App::Frame()
{
    PollRestart();
    m_jobs.RunCompleted();
    if (m_libEditTime >= 0 && ImGui::GetTime() - m_libEditTime > 0.35) { // debounce typing
        m_libEditTime = -1;
        SearchLibraries();
    }
    m_library.Pump((int)std::lround(S(40)));
    HandleShortcuts();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollWithMouse);

    m_titleH = TitleBarHeight();
    if (!m_settings.welcomed) {
        DrawWelcome();
        ImGui::End();
        DrawWindowControls();
        ui::RenderToasts();
        return;
    }

    float sidebarW = S(212);
    DrawSidebar(sidebarW);

    // Content panel: rounded top-left corner + hairline border.
    ImVec2 wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();
    ImVec2 c0(wp.x + sidebarW, wp.y + m_titleH); // the strip above it is the title bar
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = S(14);
    dl->AddRectFilled(c0, ImVec2(wp.x + ws.x + r, wp.y + ws.y + r), panel, r, ImDrawFlags_RoundCornersTopLeft);
    dl->AddRect(c0, ImVec2(wp.x + ws.x + r, wp.y + ws.y + r), border, r, S(1), ImDrawFlags_RoundCornersTopLeft);

    // Each page (and each app's editor) is a scene: switching replays its entrance.
    std::string scene = m_page == Page::Restore ? "restore" : m_page == Page::Settings ? "settings"
                      : m_editing >= 0 ? "editor:" + U8(EntryKey(m_entries[m_editing].sc)) : "pinned";
    anim::Scene(scene.c_str());

    ImGui::SetCursorScreenPos(ImVec2(c0.x + S(1), c0.y + S(1)));
    anim::Block page = anim::Begin();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(28), S(24)));
    ImGui::BeginChild("##content", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    switch (m_page) {
    case Page::Pinned:   m_editing >= 0 ? DrawEditor() : DrawPinnedPage(); break;
    case Page::Restore:  DrawRestorePage(); break;
    case Page::Settings: DrawSettingsPage(); break;
    }
    ImGui::EndChild();
    // the page as a whole fades in quickly; its parts rise in one by one (anim::Rise)
    anim::End(page, anim::Enter(0, 0.0f, 0.22f));

    DrawModals();
    ImGui::End();
    DrawWindowControls();
    ui::RenderToasts();
}

// ============================================================================
// Title bar (the window has no Windows title bar; see WM_NCCALCSIZE in main.cpp)
// ============================================================================

float App::TitleBarHeight() const { return S(46); }

App::TitleHit App::HitTestTitleBar(int x, int y) const
{
    if ((float)y >= m_titleH) return TitleHit::None;
    const TitleHit kinds[3] = { TitleHit::Minimize, TitleHit::Maximize, TitleHit::Close };
    for (int i = 0; i < 3; ++i) {
        const ImVec4& r = m_ctlRect[i];
        if (x >= r.x && x < r.z && y >= r.y && y < r.w) return kinds[i];
    }
    return TitleHit::Caption; // everything else in the strip drags the window
}

// Minimize / maximize / close, drawn above everything (even an open dialog) so the
// window can always be closed. Glyphs are drawn as lines: crisp at any DPI.
void App::DrawWindowControls()
{
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float w = S(46), h = S(34);
    const float right = ImGui::GetMainViewport()->Size.x;
    const bool maximized = IsZoomed(m_hwnd) != 0;
    const bool focused = GetForegroundWindow() == m_hwnd;
    static const char* ids[3] = { "##tb-min", "##tb-max", "##tb-close" };

    for (int i = 0; i < 3; ++i) {
        ImVec2 a(right - w * (3 - i), 0), b(a.x + w, h);
        m_ctlRect[i] = ImVec4(a.x, a.y, b.x, b.y);
        const bool hovered = io.MousePos.x >= a.x && io.MousePos.x < b.x && io.MousePos.y >= a.y && io.MousePos.y < b.y;

        // minimize and close are ordinary client-area clicks; maximize is handled in main.cpp
        if (i != 1) {
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) m_ctlHeld = i;
            if (m_ctlHeld == i && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                m_ctlHeld = -1;
                if (hovered) {
                    if (i == 0) ShowWindow(m_hwnd, SW_MINIMIZE);
                    else PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
                }
            }
        }
        const bool pressed = (i == 1 ? m_maxPressed : m_ctlHeld == i) && hovered;
        const float hov = anim::Smooth(ImHashStr(ids[i]), hovered ? 1.0f : 0.0f, 20.0f);

        ImU32 fill = 0, fg = focused ? textDim : textMuted;
        if (i == 2) { // close turns red, like Windows
            const ImU32 red = IM_COL32(196, 43, 28, 255);
            fill = FadeCol(red, hov * (pressed ? 0.85f : 1.0f));
            fg = MixCol(fg, IM_COL32_WHITE, hov);
        } else {
            fill = FadeCol(IM_COL32_WHITE, hov * (pressed ? 0.05f : 0.08f));
            fg = MixCol(fg, text, hov);
        }
        if (hov > 0) dl->AddRectFilled(a, b, fill);

        const float g = std::floor(S(10)) , t = std::max(1.0f, std::floor(S(1)));
        ImVec2 c(std::floor((a.x + b.x) * 0.5f) + 0.5f, std::floor((a.y + b.y) * 0.5f) + 0.5f);
        switch (i) {
        case 0:
            dl->AddLine(ImVec2(c.x - g * 0.5f, c.y), ImVec2(c.x + g * 0.5f, c.y), fg, t);
            break;
        case 1:
            if (maximized) { // restore: two overlapping squares
                float o = std::floor(S(2));
                dl->AddRect(ImVec2(c.x - g * 0.5f, c.y - g * 0.5f + o), ImVec2(c.x + g * 0.5f - o, c.y + g * 0.5f), fg, S(1.5f), t);
                dl->PathLineTo(ImVec2(c.x - g * 0.5f + o, c.y - g * 0.5f + o));
                dl->PathLineTo(ImVec2(c.x - g * 0.5f + o, c.y - g * 0.5f));
                dl->PathLineTo(ImVec2(c.x + g * 0.5f, c.y - g * 0.5f));
                dl->PathLineTo(ImVec2(c.x + g * 0.5f, c.y + g * 0.5f - o));
                dl->PathLineTo(ImVec2(c.x + g * 0.5f - o, c.y + g * 0.5f - o));
                dl->PathStroke(fg, t);
            } else {
                dl->AddRect(ImVec2(c.x - g * 0.5f, c.y - g * 0.5f), ImVec2(c.x + g * 0.5f, c.y + g * 0.5f), fg, S(1.5f), t);
            }
            break;
        case 2:
            dl->AddLine(ImVec2(c.x - g * 0.5f, c.y - g * 0.5f), ImVec2(c.x + g * 0.5f, c.y + g * 0.5f), fg, t);
            dl->AddLine(ImVec2(c.x + g * 0.5f, c.y - g * 0.5f), ImVec2(c.x - g * 0.5f, c.y + g * 0.5f), fg, t);
            break;
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_ctlHeld = -1;
}

void App::HandleShortcuts()
{
    if (ImGui::IsKeyChordPressed(ImGuiKey_F5) && !m_restarting) {
        Reload();
        ui::Toast(ui::ToastKind::Info, "Reloaded " + std::to_string(m_entries.size()) + " pinned shortcut(s).");
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F) && m_page == Page::Pinned) {
        CloseEditor();
        m_focusSearch = true;
    }
    if (m_page == Page::Pinned && m_editing >= 0 && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_MouseX1)) CloseEditor();
        else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O)) BrowseForIcon();
        else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S) && m_cand) ApplyCandidate();
    }
}

// ============================================================================
// Sidebar
// ============================================================================

void App::DrawSidebar(float width)
{
    // logo and name sit in the title bar strip, which also drags the window
    const float logo = S(26), ly = std::floor((m_titleH - logo) * 0.5f);
    ImGui::SetCursorPos(ImVec2(S(18), ly));
    ui::Logo(logo);
    ImGui::SameLine(0, S(10));
    ImGui::SetCursorPosY(ly + (logo - ImGui::GetFontSize() * fontH2 / fontBody) * 0.5f);
    ImGui::PushFont(fonts.bold, fontH2);
    ImGui::TextUnformatted("Iconger");
    ImGui::PopFont();
    if (ICONGER_VERSION[0] == '0') { // major version 0 = beta (see CMakeLists.txt)
        ImGui::SameLine(0, S(8));
        ImGui::SetCursorPosY(ly + (logo - S(20)) * 0.5f);
        ui::Badge("BETA", primary, primarySoft);
    }

    ImGui::SetCursorPosY(m_titleH + S(18));
    struct Item { Page page; const char* icon; const char* label; int badge; };
    const Item items[] = {
        { Page::Pinned,   ICON_PIN,      "Pinned apps", 0 },
        { Page::Restore,  ICON_HISTORY,  "Restore",     (int)m_backup.Entries().size() },
        { Page::Settings, ICON_SETTINGS, "Settings",    0 },
    };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // The highlight is drawn once, before the items (so under them), at a position
    // that glides to the active item.
    const float itemH = S(40), itemStep = itemH + ImGui::GetStyle().ItemSpacing.y - S(4);
    const float listTop = ImGui::GetCursorScreenPos().y;
    int activeIdx = 0;
    for (int i = 0; i < (int)std::size(items); ++i)
        if (items[i].page == m_page) activeIdx = i;
    const float hy = listTop + anim::Smooth("##navhl", activeIdx * itemStep, 18.0f);
    {
        ImVec2 p(ImGui::GetWindowPos().x + S(12), hy);
        dl->AddRectFilled(p, ImVec2(p.x + width - S(24), p.y + itemH), primarySoft, S(8));
        dl->AddRectFilled(ImVec2(p.x - S(12), p.y + S(8)), ImVec2(p.x - S(9), p.y + itemH - S(8)), primary, S(2));
    }
    int n = 0;
    for (const Item& it : items) {
        anim::Rise rise(m_startedAt, n++, 8.0f); // first launch: the menu rises in item by item
        ImGui::SetCursorPosX(S(12));
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 size(width - S(24), itemH);
        ImGui::PushID(it.label);
        if (ImGui::InvisibleButton("##nav", size)) {
            if (it.page == Page::Pinned && m_page == Page::Pinned) CloseEditor(); // acts as "back to list"
            m_page = it.page;
        }
        bool hovered = ImGui::IsItemHovered();
        float hov = anim::Smooth(ImGui::GetItemID(), hovered ? 1.0f : 0.0f, 16.0f);
        ImGui::PopID();
        bool active = m_page == it.page;
        if (!active && hov > 0)
            dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), FadeCol(card, hov), S(8));
        ImU32 col = active ? primary : MixCol(textSecondary, text, hov);
        float ty = p.y + (size.y - ImGui::GetFontSize()) * 0.5f;
        dl->AddText(ImVec2(p.x + S(14) + S(2) * hov, ty), col, it.icon);
        dl->AddText(ImVec2(p.x + S(44), ty), active ? text : col, it.label);
        if (it.badge > 0) {
            std::string n = std::to_string(it.badge);
            ImVec2 ts = ImGui::CalcTextSize(n.c_str());
            float bw = std::max(ts.x + S(12), S(22));
            ImVec2 b0(p.x + size.x - bw - S(8), p.y + (size.y - S(20)) * 0.5f);
            dl->AddRectFilled(b0, ImVec2(b0.x + bw, b0.y + S(20)), accentBg, S(10));
            dl->AddText(ImVec2(b0.x + (bw - ts.x) * 0.5f, b0.y + (S(20) - ts.y) * 0.5f), textDim, n.c_str());
        }
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - S(4));
    }

    DrawUpdateStatus(width);

    // Bottom: version + repo link
    float bottom = ImGui::GetWindowHeight();

    ImGui::SetCursorPos(ImVec2(S(20), bottom - S(44)));
    ImGui::PushStyleColor(ImGuiCol_Text, textMuted);
    ImGui::PushFont(nullptr, fontSmall);
    ImGui::TextUnformatted("v" ICONGER_VERSION);
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::SameLine(width - S(52));
    ImGui::SetCursorPosY(bottom - S(50));
    if (ui::IconButton("##gh", ICON_CODE, "Open the GitHub page"))
        ShellExecuteA(nullptr, "open", kRepoUrl, nullptr, nullptr, SW_SHOWNORMAL);
}

// ============================================================================
// Modals
// ============================================================================

// only one dialog is open at a time, so one timestamp is enough
static double s_modalOpenedAt = -100;

void App::DrawModals()
{
    auto beginModal = [](const char* id, float width = 420.0f) {
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(S(width), 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(24), S(22)));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, card);
        bool open = ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        if (open && ImGui::IsWindowAppearing()) s_modalOpenedAt = ImGui::GetTime();
        return open;
    };
    // dialogs open with a quick scale-up and fade (the dimmed backdrop fades on its own)
    auto endModal = [] {
        float t = anim::EnterAt(s_modalOpenedAt, 0, 0.0f, 0.24f);
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        ImVec2 c(w->Pos.x + w->Size.x * 0.5f, w->Pos.y + w->Size.y * 0.5f);
        float grow = anim::Enabled() ? 0.92f + 0.08f * anim::EaseOutBack(std::min(1.0f, (float)(ImGui::GetTime() - s_modalOpenedAt) / 0.3f)) : 1.0f;
        anim::TransformWindow(w, t, ImVec2(0, (1 - t) * S(10)), grow, c);
        ImGui::EndPopup();
    };

    if (m_openConfirm) { ImGui::OpenPopup("##confirm"); m_openConfirm = false; }
    if (beginModal("##confirm")) {
        ui::IconTile(ICON_REFRESH, warning, warningSoft, S(40));
        ImGui::SameLine(0, S(14));
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(RightEdge());
        ui::Heading("Restart Explorer?", "The taskbar and desktop disappear for a second or two. Open folder windows come back.");
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::Dummy(ImVec2(0, S(4)));
        bool dontAsk = !m_settings.confirmRestart;
        if (ui::Toggle("##dontask", &dontAsk)) { m_settings.confirmRestart = !dontAsk; m_settings.Save(); }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
        ImGui::TextUnformatted("Don't ask again");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, S(4)));
        float bw = (ImGui::GetContentRegionAvail().x - S(10)) * 0.5f;
        if (ui::Button("Not now", nullptr, ui::ButtonKind::Secondary, ImVec2(bw, S(36))) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::SameLine(0, S(10));
        if (ui::Button("Restart", ICON_REFRESH, ui::ButtonKind::Primary, ImVec2(bw, S(36))) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            StartRestart();
            ImGui::CloseCurrentPopup();
        }
        endModal();
    }

    if (m_openRestoreAll) { ImGui::OpenPopup("##restoreall"); m_openRestoreAll = false; }
    if (beginModal("##restoreall")) {
        ui::IconTile(ICON_ROTATE_CCW, danger, dangerSoft, S(40));
        ImGui::SameLine(0, S(14));
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(RightEdge());
        ui::Heading("Restore all icons?", "Every shortcut Iconger changed gets its original icon back.");
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::Dummy(ImVec2(0, S(6)));
        float bw = (ImGui::GetContentRegionAvail().x - S(10)) * 0.5f;
        if (ui::Button("Cancel", nullptr, ui::ButtonKind::Secondary, ImVec2(bw, S(36))) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::SameLine(0, S(10));
        if (ui::Button("Restore all", ICON_ROTATE_CCW, ui::ButtonKind::Danger, ImVec2(bw, S(36)))) {
            RestoreAll();
            ImGui::CloseCurrentPopup();
        }
        endModal();
    }

    if (m_openCleanup) { ImGui::OpenPopup("##cleanup"); m_openCleanup = false; }
    if (beginModal("##cleanup")) {
        ui::IconTile(ICON_TRASH, warning, warningSoft, S(40));
        ImGui::SameLine(0, S(14));
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(RightEdge());
        ui::Heading("Clean up leftovers?", "These shortcuts aren't used by the taskbar. They go to the Recycle Bin, so you can restore them.");
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::Dummy(ImVec2(0, S(6)));
        float bw = (ImGui::GetContentRegionAvail().x - S(10)) * 0.5f;
        if (ui::Button("Cancel", nullptr, ui::ButtonKind::Secondary, ImVec2(bw, S(36))) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::SameLine(0, S(10));
        if (ui::Button("Move to Recycle Bin", ICON_TRASH, ui::ButtonKind::Primary, ImVec2(bw, S(36)))) {
            ImGui::CloseCurrentPopup();
            RecycleLeftovers();
        }
        endModal();
    }

    if (m_openPinGuide) { ImGui::OpenPopup("##pinguide"); m_openPinGuide = false; }
    if (beginModal("##pinguide", 520.0f)) {
        ui::IconTile(ICON_PIN, primary, primarySoft, S(40));
        ImGui::SameLine(0, S(14));
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(RightEdge());
        ui::Heading("Two clicks left", ("Windows doesn't let apps pin to the taskbar, so swap the " + m_pinGuideName +
                                        " pin yourself:").c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::Dummy(ImVec2(0, S(2)));
        ImGui::PushTextWrapPos(RightEdge());
        ImGui::TextWrapped("1.  Right-click %s on the taskbar and choose Unpin from taskbar.", m_pinGuideName.c_str());
        ImGui::TextWrapped("2.  Open Start, search for %s (the one with your icon), right-click it and choose "
                           "Pin to taskbar. Or use the folder below: right-click the shortcut, Show more options, "
                           "Pin to taskbar.", m_pinGuideName.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
        ImGui::TextWrapped("After that it behaves like any other pin: change its icon again here any time, no re-pinning "
                           "needed, and Restore gives it the app's own icon back.");
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, S(4)));
        float bw = (ImGui::GetContentRegionAvail().x - S(10)) * 0.5f;
        if (ui::Button("Show the shortcut", ICON_FOLDER_OPEN, ui::ButtonKind::Secondary, ImVec2(bw, S(36))))
            ShowInExplorer(m_pinGuideLnk);
        ImGui::SameLine(0, S(10));
        if (ui::Button("Done", ICON_CHECK, ui::ButtonKind::Primary, ImVec2(bw, S(36))) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
            Reload(); // picks up the new pin if it's already swapped
        }
        endModal();
    }

    if (m_openUpdate) { ImGui::OpenPopup("##update"); m_openUpdate = false; }
    if (beginModal("##update", 480.0f)) {
        const bool installing = m_update == UpdateState::Installing;
        ui::IconTile(ICON_SPARKLES, primary, primarySoft, S(40));
        ImGui::SameLine(0, S(14));
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(RightEdge());
        std::string title = "Iconger v" + m_release.version + " is out";
        ui::Heading(title.c_str(), "You have v" ICONGER_VERSION ". Update now? Iconger downloads the new version "
                                   "from GitHub, then restarts.");
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();

        // release notes, lightly cleaned of markdown
        if (!m_release.notes.empty()) {
            ImGui::Dummy(ImVec2(0, S(2)));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(10));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(14), S(10)));
            ImGui::BeginChild("##notes", ImVec2(0, S(190)), ImGuiChildFlags_AlwaysUseWindowPadding);
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();
            ImGui::PushTextWrapPos(0);
            size_t pos = 0;
            const std::string& n = m_release.notes;
            while (pos < n.size()) {
                size_t eol = n.find('\n', pos);
                if (eol == std::string::npos) eol = n.size();
                std::string line = n.substr(pos, eol - pos);
                pos = eol + 1;
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty() || line.find("iconger.exe` below") != std::string::npos) continue;
                for (const char* mark : { "**", "`" })
                    for (size_t m; (m = line.find(mark)) != std::string::npos;) line.erase(m, strlen(mark));
                if (line.rfind("#", 0) == 0) {
                    line.erase(0, line.find_first_not_of("# "));
                    ImGui::Dummy(ImVec2(0, S(2)));
                    ImGui::PushFont(fonts.semibold, 0);
                    ImGui::TextUnformatted(line.c_str());
                    ImGui::PopFont();
                } else if (line.rfind("- ", 0) == 0 || line.rfind("* ", 0) == 0) {
                    ImGui::PushStyleColor(ImGuiCol_Text, textDim);
                    ImGui::Bullet();
                    ImGui::SameLine();
                    ImGui::TextWrapped("%s", line.c_str() + 2);
                    ImGui::PopStyleColor();
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, textDim);
                    ImGui::TextWrapped("%s", line.c_str());
                    ImGui::PopStyleColor();
                }
            }
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
        }

        ImGui::Dummy(ImVec2(0, S(4)));
        if (installing) {
            ui::Spinner(S(18), primary);
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
            ImGui::TextUnformatted("Downloading and installing...");
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, S(2)));
        } else if (!m_updateError.empty()) {
            ImGui::PushTextWrapPos(RightEdge());
            ImGui::PushStyleColor(ImGuiCol_Text, danger);
            ImGui::TextWrapped("Update failed: %s", m_updateError.c_str());
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
            if (ui::Button("Download it from GitHub instead", ICON_EXTERNAL, ui::ButtonKind::Ghost) && !m_release.pageUrl.empty())
                ShellExecuteA(nullptr, "open", m_release.pageUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        float bw = (ImGui::GetContentRegionAvail().x - S(10)) * 0.5f;
        if (ui::Button("Not now", nullptr, ui::ButtonKind::Secondary, ImVec2(bw, S(36)), !installing) ||
            (!installing && ImGui::IsKeyPressed(ImGuiKey_Escape)))
            ImGui::CloseCurrentPopup();
        const bool skipped = Utf8ToWide(m_release.version) == m_settings.skippedVersion;
        ImGui::SameLine(0, S(10));
        if (ui::Button("Update and restart", ICON_DOWNLOAD, ui::ButtonKind::Primary, ImVec2(bw, S(36)), !installing))
            InstallUpdate();
        if (!installing && !skipped) {
            // centred text link under the buttons
            const char* skip = "Skip this version";
            float tw = ImGui::CalcTextSize(skip).x + ImGui::GetStyle().FramePadding.x * 2;
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - tw) * 0.5f);
            if (ui::Button(skip, nullptr, ui::ButtonKind::Ghost)) {
                m_settings.skippedVersion = Utf8ToWide(m_release.version);
                m_settings.Save();
                ImGui::CloseCurrentPopup();
                ui::Toast(ui::ToastKind::Info, "Skipped v" + m_release.version + ". You can still update from Settings.");
            }
            ui::Tooltip("Don't ask about this version at startup again. A newer one will still show up.");
        }
        endModal();
    }

    // blocking overlay while Explorer restarts
    if (m_restarting && !ImGui::IsPopupOpen("##restarting")) ImGui::OpenPopup("##restarting");
    if (beginModal("##restarting")) {
        ui::Spinner(S(28), primary);
        ImGui::SameLine(0, S(14));
        ImGui::BeginGroup();
        ui::Heading("Restarting Explorer", "Your taskbar will be back in a moment.");
        ImGui::EndGroup();
        if (!m_restarting) ImGui::CloseCurrentPopup();
        endModal();
    }
}
