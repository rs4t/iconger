// "Settings" page.
#include "app_internal.h"
#include <optional>

using namespace appui;
using Microsoft::WRL::ComPtr;

// ============================================================================
// Settings page
// ============================================================================

void App::DrawSettingsPage()
{
    {
        anim::Rise rise(0);
        ImGui::PushFont(fonts.bold, fontH1);
        ImGui::TextUnformatted("Settings");
        ImGui::PopFont();
    }
    ImGui::Dummy(ImVec2(0, S(4)));

    ImGui::BeginChild("##settings", ImVec2(0, 0), 0, ImGuiWindowFlags_NoBackground);
    float maxW = std::min(ImGui::GetContentRegionAvail().x, S(760));
    std::optional<anim::Rise> card; // the cards rise in one after another
    int cardIndex = 1;

    card.emplace(cardIndex++, 18.0f);
    ui::BeginCard("##libraries", ImVec2(maxW, 0), 20);
    ui::IconTile(ICON_SEARCH, success, successSoft, S(40));
    ImGui::SameLine(0, S(14));
    ImGui::BeginGroup();
    ui::Heading("Icon libraries", "About 20,000 app icons in seven styles (Dashboard Icons, WhiteSur, Fluent, Papirus, "
                                  "Tela, Candy, Simple Icons), searched by app name in \"This app\". Only the icons "
                                  "you look at are downloaded, and they're cached.");
    ImGui::EndGroup();
    ImGui::Separator();
    if (ui::SettingRow("Search online icon libraries",
                       "Turn off to keep Iconger fully offline.", &m_settings.onlineLibraries)) {
        m_settings.Save();
        if (m_settings.onlineLibraries) { m_library.EnsureIndex(); SearchLibraries(); }
    }
    if (ui::Button("Clear downloaded icons", ICON_TRASH, ui::ButtonKind::Secondary)) {
        // only the download cache; applied icons live in the icons folder and stay put
        std::wstring dir = DataDir() + L"\\cache";
        WIN32_FIND_DATAW ffd = {};
        HANDLE h = FindFirstFileW((dir + L"\\*.bin").c_str(), &ffd);
        int n = 0;
        if (h != INVALID_HANDLE_VALUE) {
            do { if (DeleteFileW((dir + L"\\" + ffd.cFileName).c_str())) ++n; } while (FindNextFileW(h, &ffd));
            FindClose(h);
        }
        m_library.Reset();
        if (m_settings.onlineLibraries && m_editing >= 0) { m_library.EnsureIndex(); SearchLibraries(); }
        ui::Toast(ui::ToastKind::Success, "Cleared " + std::to_string(n) + " downloaded file(s).");
    }
    ui::Tooltip("Icons you already applied are kept.");
    ui::EndCard();
    card.reset();

    ImGui::Spacing();
    card.emplace(cardIndex++, 18.0f);
    ui::BeginCard("##updates", ImVec2(maxW, 0), 20);
    ui::IconTile(ICON_DOWNLOAD, warning, warningSoft, S(40));
    ImGui::SameLine(0, S(14));
    ImGui::BeginGroup();
    ui::Heading("Updates and Start menu", "Iconger can keep itself up to date, and shows up in Windows search like any app.");
    ImGui::EndGroup();
    {
        const bool busy = m_update == UpdateState::Checking || m_update == UpdateState::Installing;
        if (m_update == UpdateState::Available) {
            std::string label = "Update to v" + m_release.version;
            if (ui::Button(label.c_str(), ICON_DOWNLOAD, ui::ButtonKind::Primary)) m_openUpdate = true;
        } else if (ui::Button(m_update == UpdateState::Checking ? "Checking..." : "Check for updates", ICON_REFRESH,
                              ui::ButtonKind::Outline, ImVec2(0, 0), !busy)) {
            StartUpdateCheck(true);
        }
    }
    ImGui::Separator();
    if (ui::SettingRow("Check for updates at startup",
                       "Each time Iconger opens, it asks GitHub whether there's a newer version.", &m_settings.checkUpdates))
        m_settings.Save();
    if (ui::SettingRow("Show Iconger in the Start menu",
                       "So you can find it by searching \"Iconger\" in Windows, and pin it from there.", &m_settings.startMenuShortcut)) {
        m_settings.Save();
        if (m_settings.startMenuShortcut) {
            if (!EnsureStartMenuShortcut(ExePath())) ui::Toast(ui::ToastKind::Error, "Couldn't add Iconger to the Start menu.");
        } else {
            RemoveStartMenuShortcut();
        }
    }
    ui::EndCard();
    card.reset();

    ImGui::Spacing();
    card.emplace(cardIndex++, 18.0f);
    ui::BeginCard("##storage", ImVec2(maxW, 0), 20);
    ui::IconTile(ICON_FOLDER_OPEN, violet, violetSoft, S(40));
    ImGui::SameLine(0, S(14));
    ImGui::BeginGroup();
    ui::Heading("Folders", "Where Iconger keeps things.");
    ImGui::EndGroup();
    ImGui::Separator();
    auto folderRow = [&](const char* label, const std::wstring& path) {
        ImGui::PushID(label);
        float bw = S(100);
        ImGui::BeginGroup();
        ImGui::PushFont(fonts.semibold, 0);
        ImGui::TextUnformatted(label);
        ImGui::PopFont();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - S(6));
        ui::TextEllipsis(U8(path).c_str(), ImGui::GetContentRegionAvail().x - bw - S(16), textSecondary);
        ImGui::EndGroup();
        ImGui::SameLine(RightEdge() - bw);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(6));
        if (ui::Button("Open", ICON_EXTERNAL, ui::ButtonKind::Secondary, ImVec2(bw, 0))) OpenPath(path);
        ImGui::PopID();
    };
    folderRow("Converted icons", IconsDir());
    folderRow("Pinned shortcuts", GetPinnedShortcutsFolder());
    ui::EndCard();
    card.reset();

    card.emplace(cardIndex++, 18.0f);
    ui::BeginCard("##experimental", ImVec2(maxW, 0), 20);
    ui::IconTile(ICON_SPARKLES, violet, violetSoft, S(40));
    ImGui::SameLine(0, S(14));
    ImGui::BeginGroup();
    ImGui::PushFont(fonts.semibold, fontH2);
    ImGui::TextUnformatted("Apps that aren't pinned");
    ImGui::PopFont();
    ImGui::SameLine(0, S(10));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(2));
    ui::Badge("EXPERIMENTAL", violet, violetSoft);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - S(8));
    ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
    ImGui::TextWrapped("Custom icons for apps that are on the taskbar only while they run. Iconger swaps the icon of "
                       "their windows, so it has to keep running in the background. Off by default while it's being "
                       "tested: some apps may not take the new icon, or flash their own for a moment.");
    ImGui::PopStyleColor();
    ImGui::EndGroup();
    ImGui::Separator();
    {
        bool on = m_settings.unpinnedIcons;
        if (ui::SettingRow("Custom icons for apps that aren't pinned",
                           "Adds them to Pinned apps under \"Running now\". Closing Iconger's window then keeps it "
                           "running in the background (you'll see it in Task Manager).", &on))
            SetUnpinnedIcons(on);
    }
    if (m_settings.unpinnedIcons) {
        bool startup = m_startsWithWindows;
        if (ui::SettingRow("Start with Windows",
                           "Starts Iconger in the background when you sign in, so custom icons are back after a restart.",
                           &startup)) {
            if (!SetStartWithWindows(startup, ExePath())) ui::Toast(ui::ToastKind::Error, "Couldn't change the startup setting.");
            m_startsWithWindows = StartsWithWindows();
        }
        if (!m_keeper.Blocked().empty()) {
            std::string names;
            for (const auto& exe : m_keeper.Blocked()) names += (names.empty() ? "" : ", ") + U8(ExeDisplayName(exe));
            ImGui::PushTextWrapPos(0);
            ImGui::PushStyleColor(ImGuiCol_Text, warning);
            ImGui::TextWrapped(ICON_ALERT "  Windows didn't let Iconger change: %s (running as administrator).", names.c_str());
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
        }
        if (ui::Button("Quit Iconger completely", ICON_POWER, ui::ButtonKind::Secondary))
            m_quit = true; // custom icons of unpinned apps go back to normal until Iconger runs again
        ui::Tooltip("Stops Iconger, including in the background. Apps that aren't pinned get their own icons back "
                    "until Iconger runs again.");
    }
    ui::EndCard();
    card.reset();

    // a fallback: icons normally apply instantly, so this sits below the everyday settings
    ImGui::Spacing();
    card.emplace(cardIndex++, 18.0f);
    ui::BeginCard("##apply", ImVec2(maxW, 0), 20);
    ui::IconTile(ICON_MONITOR_COG, primary, primarySoft, S(40));
    ImGui::SameLine(0, S(14));
    ImGui::BeginGroup();
    ui::Heading("Icon not updating?", "New icons normally show up on the taskbar right away. If one gets stuck, "
                                      "restarting Explorer makes Windows reload every icon.");
    ImGui::EndGroup();
    if (ui::Button(m_restarting ? "Restarting..." : "Restart Explorer", ICON_REFRESH, ui::ButtonKind::Outline,
                   ImVec2(0, 0), !m_restarting))
        RequestRestart();
    ImGui::Separator();
    bool changed = false;
    changed |= ui::SettingRow("Ask before restarting",
        "Explorer restarting makes the taskbar and desktop blink for a second.", &m_settings.confirmRestart);
    changed |= ui::SettingRow("Clear the icon cache",
        "Deletes Windows' iconcache files during the restart. Fixes icons that refuse to update.", &m_settings.clearIconCache);
    ui::EndCard();
    card.reset();
    if (changed) m_settings.Save();

    ImGui::Spacing();
    card.emplace(cardIndex++, 18.0f);
    ui::BeginCard("##about", ImVec2(maxW, 0), 20);
    ui::Logo(S(40));
    ImGui::SameLine(0, S(14));
    ImGui::BeginGroup();
    ui::Heading("Iconger v" ICONGER_VERSION, "Open source (MIT). Shortcuts: F5 reload, Ctrl+F search, Ctrl+O browse, Ctrl+S apply, Esc back.");
    ImGui::EndGroup();
    if (ui::Button("GitHub", ICON_CODE, ui::ButtonKind::Outline))
        ShellExecuteA(nullptr, "open", kRepoUrl, nullptr, nullptr, SW_SHOWNORMAL);
    ui::EndCard();
    card.reset();
    ImGui::EndChild();
}
