// "Pinned apps" page: the list of pinned shortcuts.
#include "app_internal.h"

using namespace appui;
using Microsoft::WRL::ComPtr;

// ============================================================================
// Pinned apps page
// ============================================================================

void App::DrawPinnedPage()
{
    // header
    float x0 = ImGui::GetCursorPosX(); // SameLine(x) counts from the window edge, not the padded content
    float avail = ImGui::GetContentRegionAvail().x;
    {
        anim::Rise rise(0);
        ImGui::BeginGroup();
        ImGui::PushFont(fonts.bold, fontH1);
        ImGui::TextUnformatted("Pinned apps");
        ImGui::PopFont();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - S(8));
        ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
        size_t live = std::count_if(m_entries.begin(), m_entries.end(),
                                    [](const Entry& e) { return e.sc.onTaskbar && !e.sc.running; });
        ImGui::Text("%zu app%s on your taskbar. Pick one to change its icon.", live, live == 1 ? "" : "s");
        ImGui::PopStyleColor();
        ImGui::EndGroup();

        float searchW = std::min(S(260), avail * 0.35f);
        float btn = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2;
        ImGui::SameLine(x0 + avail - searchW - btn * 2 - ImGui::GetStyle().ItemSpacing.x * 2);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(6));
        ImGui::SetNextItemWidth(searchW);
        if (m_focusSearch) { ImGui::SetKeyboardFocusHere(); m_focusSearch = false; }
        ImGui::InputTextWithHint("##search", ICON_SEARCH "  Search  (Ctrl+F)", m_search, sizeof(m_search));
        ImGui::SameLine();
        if (ui::IconButton("##reload", ICON_REFRESH, "Reload the list (F5)")) Reload();
        ImGui::SameLine();
        if (ui::IconButton("##folder", ICON_FOLDER_OPEN, "Open the pinned shortcuts folder"))
            OpenPath(GetPinnedShortcutsFolder());
    }
    ImGui::Dummy(ImVec2(0, S(6)));

    if (m_entries.empty()) {
        anim::Rise rise(1);
        ui::BeginCard("##empty");
        ui::IconTile(ICON_PIN, primary, primarySoft, S(44));
        ImGui::SameLine(0, S(14));
        ImGui::BeginGroup();
        ui::Heading("No pinned shortcuts found", "Pin an app to the taskbar (right-click it > Pin to taskbar), then press F5.");
        ImGui::EndGroup();
        ui::EndCard();
        return;
    }

    ImGui::BeginChild("##grid", ImVec2(0, 0), 0, ImGuiWindowFlags_NoBackground);
    float gap = S(12);
    float width = ImGui::GetContentRegionAvail().x;
    int cols = std::max(1, (int)((width + gap) / (S(240) + gap)));
    float cardW = std::floor((width - gap * (cols - 1)) / cols);
    int shown = 0;
    // sections: pins, running apps that aren't pinned (experimental), leftover shortcuts
    auto section = [](const Entry& e) { return e.sc.running ? 1 : e.sc.onTaskbar ? 0 : 2; };
    for (int pass = 0; pass < 3; ++pass) {
        int inSection = 0;
        for (int i = 0; i < (int)m_entries.size(); ++i) {
            if (section(m_entries[i]) != pass) continue;
            if (!ContainsNoCase(U8(m_entries[i].sc.displayName), m_search)) continue;
            if (pass == 1 && inSection == 0) {
                ImGui::Dummy(ImVec2(0, S(12)));
                anim::Rise rise(1 + shown);
                ImGui::BeginGroup();
                ImGui::PushFont(fonts.semibold, fontH2);
                ImGui::TextUnformatted("Running now");
                ImGui::PopFont();
                ImGui::SameLine(0, S(10));
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(2));
                ui::Badge("EXPERIMENTAL", violet, violetSoft);
                ImGui::PushTextWrapPos(width);
                ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - S(6));
                ImGui::TextWrapped(m_settings.unpinnedIcons
                    ? "On your taskbar but not pinned. Their new icon comes back each time you open them, while Iconger runs in the background."
                    : "On your taskbar but not pinned. Changing these needs an experimental feature: pick one to turn it on.");
                ImGui::PopStyleColor();
                ImGui::PopTextWrapPos();
                ImGui::EndGroup();
            }
            if (pass == 2 && inSection == 0) {
                // section header for shortcuts Windows orphaned on re-pin
                ImGui::Dummy(ImVec2(0, S(12)));
                anim::Rise rise(1 + shown);
                ImGui::BeginGroup();
                ui::Heading("Leftover shortcuts",
                            "Windows left these behind when an app was re-pinned. The taskbar ignores them.");
                ImGui::EndGroup();
                float bw = S(200);
                ImGui::SameLine(width - bw);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(6));
                if (ui::Button("Move to Recycle Bin", ICON_TRASH, ui::ButtonKind::Secondary, ImVec2(bw, 0)))
                    m_openCleanup = true;
            }
            if (inSection % cols != 0) ImGui::SameLine(0, gap);
            {
                anim::Rise rise(1 + shown, 18.0f);
                DrawAppCard(i, cardW);
            }
            ++inSection;
            ++shown;
        }
    }
    if (shown == 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
        ImGui::Text("Nothing matches \"%s\".", m_search);
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
}

void App::DrawAppCard(int index, float width)
{
    Entry& e = m_entries[index];
    ImGui::PushID(index);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size(width, S(72));
    bool clicked = ImGui::InvisibleButton("##card", size);
    bool hovered = ImGui::IsItemHovered();
    ImGuiID id = ImGui::GetItemID();
    ImGui::PopID();
    // hover lifts the card and warms its colours; pressing sinks it back
    const float hov = anim::Smooth(id, hovered ? 1.0f : 0.0f, 16.0f);
    const float press = anim::Smooth(id + 1, ImGui::IsItemActive() ? 1.0f : 0.0f, 28.0f);
    // just applied / restored: the icon pops and the card glows green for a moment
    const float done = EntryKey(e.sc) == m_appliedKey ? Bump(m_appliedAt, 0.7f) : 0.0f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    anim::Block block = anim::Begin();
    ImVec2 q(p.x + size.x, p.y + size.y);
    if (hov > 0) // soft shadow under the lifted card
        dl->AddRectFilled(ImVec2(p.x + S(2), p.y + S(6)), ImVec2(q.x - S(2), q.y + S(4)),
                          IM_COL32(0, 0, 0, (int)(70 * hov)), S(12));
    dl->AddRectFilled(p, q, MixCol(card, cardHover, hov), S(12));
    dl->AddRect(p, q, MixCol(MixCol(border, borderStrong, hov), success, done), S(12), S(1) + S(1) * done);

    float icon = S(40);
    ImVec2 ip(p.x + S(16), p.y + (size.y - icon) * 0.5f);
    ImU32 tint = e.sc.onTaskbar ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 110);
    {
        anim::Block ib = anim::Begin();
        if (e.icon) dl->AddImage(e.icon.Id(), ip, ImVec2(ip.x + icon, ip.y + icon), ImVec2(0, 0), ImVec2(1, 1), tint);
        else dl->AddRectFilled(ip, ImVec2(ip.x + icon, ip.y + icon), accentBg, S(8));
        float grow = 1.0f + 0.06f * hov + 0.28f * done;
        anim::End(ib, 1.0f, ImVec2(0, 0), grow, ImVec2(ip.x + icon * 0.5f, ip.y + icon * 0.5f));
    }

    float tx = ip.x + icon + S(14);
    float textW = q.x - tx - S(34);
    std::string name = U8(e.sc.displayName);
    ImGui::PushFont(fonts.semibold, 0);
    ImGui::RenderTextEllipsis(dl, ImVec2(tx, p.y + S(15)), ImVec2(tx + textW, p.y + S(40)), tx + textW, name.c_str(), nullptr, nullptr);
    ImGui::PopFont();

    ImGui::PushFont(nullptr, fontSmall);
    float y2 = p.y + S(40);
    if (!e.sc.onTaskbar) {
        std::string sub = "Unused copy: " + U8(FileStem(e.sc.lnkPath)) + ".lnk";
        ImGui::PushStyleColor(ImGuiCol_Text, textMuted);
        ImGui::RenderTextEllipsis(dl, ImVec2(tx, y2), ImVec2(tx + textW, y2 + S(20)), tx + textW, sub.c_str(), nullptr, nullptr);
        ImGui::PopStyleColor();
    } else if (IsCustomized(e) && e.sc.running && !m_settings.unpinnedIcons) {
        dl->AddText(ImVec2(tx, y2), textMuted, "Custom icon (feature off)");
    } else if (IsCustomized(e)) {
        dl->AddText(ImVec2(tx, y2), success, ICON_CHECK " Custom icon");
    } else if (e.sc.running) {
        dl->AddText(ImVec2(tx, y2), textSecondary, "Running, not pinned");
    } else {
        std::string sub = e.sc.packaged ? "Store app" : e.sc.targetPath.empty() ? "Windows app"
                        : U8(FileStem(e.sc.targetPath) + LowerExt(e.sc.targetPath));
        ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
        ImGui::RenderTextEllipsis(dl, ImVec2(tx, y2), ImVec2(tx + textW, y2 + S(20)), tx + textW, sub.c_str(), nullptr, nullptr);
        ImGui::PopStyleColor();
    }
    ImGui::PopFont();

    ImVec2 cs = ImGui::CalcTextSize(ICON_CHEVRON_RIGHT);
    dl->AddText(ImVec2(q.x - S(16) - cs.x + S(4) * hov, p.y + (size.y - cs.y) * 0.5f), MixCol(textMuted, text, hov),
                ICON_CHEVRON_RIGHT);
    anim::End(block, 1.0f, ImVec2(0, -S(3) * hov * (1.0f - press)), 1.0f - 0.02f * press,
              ImVec2((p.x + q.x) * 0.5f, (p.y + q.y) * 0.5f));

    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (clicked) {
        if (e.sc.running && !m_settings.unpinnedIcons) {
            // needs the experimental feature first: ask, then open it
            m_pendingOpenKey = EntryKey(e.sc);
            m_openEnableUnpinned = true;
        } else {
            OpenEditor(index);
        }
    }
}
