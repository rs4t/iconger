// "Restore" page.
#include "app_internal.h"

using namespace appui;
using Microsoft::WRL::ComPtr;

// ============================================================================
// Restore page
// ============================================================================

void App::DrawRestorePage()
{
    float x0 = ImGui::GetCursorPosX();
    float avail = ImGui::GetContentRegionAvail().x;
    const auto& backups = m_backup.Entries();
    {
        anim::Rise rise(0);
        ImGui::BeginGroup();
        ImGui::PushFont(fonts.bold, fontH1);
        ImGui::TextUnformatted("Restore");
        ImGui::PopFont();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - S(8));
        ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
        ImGui::TextUnformatted("Iconger remembers every shortcut's original icon before changing it.");
        ImGui::PopStyleColor();
        ImGui::EndGroup();

        if (!backups.empty() || !m_winRules.All().empty()) {
            float bw = S(150);
            ImGui::SameLine(x0 + avail - bw);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(8));
            if (ui::Button("Restore all", ICON_ROTATE_CCW, ui::ButtonKind::Danger, ImVec2(bw, 0))) m_openRestoreAll = true;
        }
    }
    ImGui::Dummy(ImVec2(0, S(6)));

    if (backups.empty() && m_winRules.All().empty()) {
        anim::Rise rise(1);
        ui::BeginCard("##none");
        ui::IconTile(ICON_ARCHIVE, success, successSoft, S(44));
        ImGui::SameLine(0, S(14));
        ImGui::BeginGroup();
        ui::Heading("Nothing to restore", "Every pinned shortcut still has its original icon.");
        ImGui::EndGroup();
        ui::EndCard();
        return;
    }

    std::wstring folder = GetPinnedShortcutsFolder();
    std::wstring restoreLnk, forgetLnk;
    ImGui::BeginChild("##list", ImVec2(0, 0), 0, ImGuiWindowFlags_NoBackground);
    int row = 1;
    for (const auto& [key, b] : backups) {
        anim::Rise rise(row++, 16.0f);
        std::wstring lnk = folder + L"\\" + key;
        const Entry* entry = nullptr;
        for (const auto& e : m_entries)
            if (_wcsicmp(e.sc.lnkPath.c_str(), lnk.c_str()) == 0) entry = &e;

        ImGui::PushID(U8(key).c_str());
        ui::BeginCard("##row", ImVec2(0, 0), 14);
        float icon = S(40);
        if (entry && entry->icon) DrawTexture(entry->icon, icon);
        else ui::IconTile(ICON_PIN, textMuted, accentBg, icon);
        ImGui::SameLine(0, S(14));
        ImGui::BeginGroup();
        ImGui::PushFont(fonts.semibold, 0);
        ImGui::TextUnformatted(entry ? U8(entry->sc.displayName).c_str() : U8(FileStem(key)).c_str());
        ImGui::PopFont();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - S(8));
        std::string orig = b.iconPath.empty() ? "the app's own icon"
                         : U8(FileStem(b.iconPath) + LowerExt(b.iconPath)) + (b.iconIndex ? " #" + std::to_string(b.iconIndex) : "");
        ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
        if (entry) ImGui::Text("Original: %s", orig.c_str());
        else ImGui::TextUnformatted("No longer pinned");
        ImGui::PopStyleColor();
        ImGui::EndGroup();

        float bw = S(120);
        ImGui::SameLine(RightEdge() - bw);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (icon - ImGui::GetFrameHeight()) * 0.5f);
        if (entry) {
            if (ui::Button("Restore", ICON_UNDO, ui::ButtonKind::Secondary, ImVec2(bw, 0))) restoreLnk = lnk;
        } else if (ui::Button("Forget", ICON_X, ui::ButtonKind::Ghost, ImVec2(bw, 0))) {
            forgetLnk = lnk;
        }
        ui::EndCard();
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, S(2)));
    }

    // EXPERIMENTAL: running apps that aren't pinned. Nothing on disk was changed for
    // them, so "restoring" just removes the rule and gives open windows their icons back.
    std::wstring restoreExe;
    for (const auto& [exe, ico] : m_winRules.All()) {
        anim::Rise rise(row++, 16.0f);
        ImGui::PushID(U8(exe).c_str());
        ui::BeginCard("##rule", ImVec2(0, 0), 14);
        float icon = S(40);
        const Entry* entry = nullptr;
        for (const auto& e : m_entries)
            if (e.sc.running && WindowIconRules::Key(e.sc.targetPath) == exe) entry = &e;
        if (entry && entry->icon) DrawTexture(entry->icon, icon);
        else ui::IconTile(ICON_APP_WINDOW, textMuted, accentBg, icon);
        ImGui::SameLine(0, S(14));
        ImGui::BeginGroup();
        ImGui::PushFont(fonts.semibold, 0);
        ImGui::TextUnformatted(U8(ExeDisplayName(exe)).c_str());
        ImGui::PopFont();
        ImGui::SameLine(0, S(8));
        ui::Badge("EXPERIMENTAL", violet, violetSoft);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - S(8));
        ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
        ImGui::TextUnformatted(m_settings.unpinnedIcons ? "Not pinned: the icon is applied while Iconger runs"
                                                        : "Not pinned: paused (the experimental feature is off)");
        ImGui::PopStyleColor();
        ImGui::EndGroup();
        float bw = S(120);
        ImGui::SameLine(RightEdge() - bw);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (icon - ImGui::GetFrameHeight()) * 0.5f);
        if (ui::Button("Restore", ICON_UNDO, ui::ButtonKind::Secondary, ImVec2(bw, 0))) restoreExe = exe;
        ui::EndCard();
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, S(2)));
    }
    ImGui::EndChild();

    // mutate after iterating the maps
    if (!restoreExe.empty()) {
        std::string name = U8(ExeDisplayName(restoreExe));
        RestoreRunningApp(restoreExe);
        ui::Toast(ui::ToastKind::Success, "Restored the original icon of " + name + ".");
    }
    if (!restoreLnk.empty()) RestoreOriginal(restoreLnk);
    if (!forgetLnk.empty()) { m_backup.Remove(forgetLnk); m_backup.Save(); }
}
