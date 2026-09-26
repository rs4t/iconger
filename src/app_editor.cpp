// Icon editor: preview, adjustments, and the icon sources (this app, libraries, files).
#include "app_internal.h"
#include <optional>

using namespace appui;
using Microsoft::WRL::ComPtr;

// ============================================================================
// Editor
// ============================================================================

void App::DrawEditor()
{
    Entry& e = m_entries[m_editing];
    {
        anim::Rise rise(0);
        if (ui::Button("Pinned apps", ICON_ARROW_LEFT, ui::ButtonKind::Ghost)) { CloseEditor(); return; }
        ui::Tooltip("Back (Esc)");

        ImGui::PushFont(fonts.bold, fontH1);
        ImGui::TextUnformatted(U8(e.sc.displayName).c_str());
        ImGui::PopFont();
        ImGui::SameLine(0, S(12));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(8));
        // the badge flips to CUSTOM with a little pop when an icon is applied
        ImVec2 bp = ImGui::GetCursorScreenPos();
        const float badgeY = ImGui::GetCursorPosY();
        anim::Block bb = anim::Begin();
        if (IsCustomized(e)) ui::Badge("CUSTOM ICON", success, successSoft);
        else ui::Badge("ORIGINAL ICON", textDim, accentBg);
        float pop = EntryKey(e.sc) == m_appliedKey ? Bump(m_appliedAt, 0.45f) : 0.0f;
        anim::End(bb, 1.0f, ImVec2(0, 0), 1.0f + 0.15f * pop, ImVec2(bp.x + S(50), bp.y + S(10)));
        if (e.sc.running) {
            ImGui::SameLine(0, S(6));
            ImGui::SetCursorPosY(badgeY); // line up with the badge before it
            ui::Badge("EXPERIMENTAL", violet, violetSoft);
            ui::Tooltip("Not pinned: Iconger swaps the icon of this app's windows while it runs in the background.");
        }
    }

    ImGui::Dummy(ImVec2(0, S(4)));
    if (!e.sc.onTaskbar) {
        anim::Rise rise(1);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, warningSoft);
        ImGui::PushStyleColor(ImGuiCol_Border, warningBorder);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(14), S(10)));
        ImGui::BeginChild("##leftover", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        ImGui::PushStyleColor(ImGuiCol_Text, warning);
        ImGui::TextUnformatted(ICON_ALERT);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextWrapped("This is an unused copy that Windows left behind. The taskbar won't show changes made here; "
                           "edit the app in the list above instead.");
        ImGui::EndChild();
        ImGui::Spacing();
    }
    float leftW =std::max(S(300), std::min(S(360), ImGui::GetContentRegionAvail().x * 0.38f));
    ImGui::BeginChild("##left", ImVec2(leftW, 0), 0, ImGuiWindowFlags_NoBackground);
    DrawPreviewCard();
    ImGui::EndChild();
    ImGui::SameLine(0, S(16));
    ImGui::BeginChild("##right", ImVec2(0, 0), 0, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
    {
        anim::Rise rise(3, 18.0f);
        DrawSourceCard();
    }
    ImGui::EndChild();
}

void App::DrawPreviewCard()
{
    Entry& e = m_entries[m_editing];
    std::optional<anim::Rise> rise(std::in_place, 2, 18.0f);
    ui::BeginCard("##preview");
    ui::Heading("Preview");
    ImGui::Dummy(ImVec2(0, S(2)));

    float tile = S(96);
    float avail = ImGui::GetContentRegionAvail().x;
    float arrowW = S(40);
    float startX = ImGui::GetCursorPosX() + (avail - tile * 2 - arrowW) * 0.5f;
    ImGui::SetCursorPosX(startX);
    {
        // "Current" pops when an icon was just applied or restored
        ImVec2 cp = ImGui::GetCursorScreenPos();
        anim::Block b = anim::Begin();
        IconFrame(m_editingPreview, tile, "Current", false);
        float pop = EntryKey(e.sc) == m_appliedKey ? Bump(m_appliedAt, 0.6f) : 0.0f;
        if (pop > 0) {
            ImDrawList* dl = ImGui::GetWindowDrawList(); // a green ring that swells and fades
            float grow = S(6) * pop;
            dl->AddRect(ImVec2(cp.x - grow, cp.y - grow), ImVec2(cp.x + tile + grow, cp.y + tile + grow),
                        FadeCol(success, pop), S(14) + grow, S(2));
        }
        anim::End(b, 1.0f, ImVec2(0, 0), 1.0f + 0.08f * pop, ImVec2(cp.x + tile * 0.5f, cp.y + tile * 0.5f));
    }
    ImGui::SameLine(0, 0);
    ImVec2 ap = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(arrowW, tile));
    ImVec2 as = ImGui::CalcTextSize(ICON_ARROW_RIGHT);
    // the arrow darts across whenever a new icon is picked
    float dart = m_cand ? 1.0f - anim::EnterAt(m_candAt, 0, 0.0f, 0.35f) : 0.0f;
    ImGui::GetWindowDrawList()->AddText(ImVec2(ap.x + (arrowW - as.x) * 0.5f - S(10) * dart, ap.y + (tile - as.y) * 0.5f),
                                        FadeCol(m_cand ? primary : textMuted, 1.0f - dart), ICON_ARROW_RIGHT);
    ImGui::SameLine(0, 0);
    {
        // "New" pops in with a little overshoot each time an icon is picked
        ImVec2 np = ImGui::GetCursorScreenPos();
        anim::Block b = anim::Begin();
        IconFrame(m_cand.preview, tile, m_cand ? "New" : "Pick an icon", (bool)m_cand);
        float t = m_cand && anim::Enabled() ? std::min(1.0f, (float)(ImGui::GetTime() - m_candAt) / 0.4f) : 1.0f;
        if (t < 1) ui::KeepAnimating(0.05f);
        anim::End(b, 1.0f, ImVec2(0, 0), 0.8f + 0.2f * anim::EaseOutBack(t, 2.4f),
                  ImVec2(np.x + tile * 0.5f, np.y + tile * 0.5f));
    }

    ImGui::Dummy(ImVec2(0, S(6)));
    DrawTaskbarPreview(avail);
    ImGui::Dummy(ImVec2(0, S(4)));

    if (m_cand) {
        DrawAdjustPanel();
    } else if (ui::Button("Customize current icon", ICON_SLIDERS, ui::ButtonKind::Secondary, ImVec2(-1, 0))) {
        CustomizeCurrentIcon();
    } else {
        ui::Tooltip("Recolour the icon it has now: hue, saturation, brightness, contrast, tint");
    }
    ImGui::Dummy(ImVec2(0, S(4)));

    if (ui::Button("Apply icon", ICON_CHECK, ui::ButtonKind::Primary, ImVec2(-1, S(38)), (bool)m_cand))
        ApplyCandidate();
    ui::Tooltip(m_cand ? "Save this icon to the shortcut (Ctrl+S)" : "Choose an icon on the right first");

    bool customized = IsCustomized(e);
    std::wstring curPath;
    int curIndex = 0;
    ResolveShortcutIcon(e.sc, curPath, curIndex);
    bool usesOwnIcon = curIndex == 0 && _wcsicmp(curPath.c_str(), e.sc.targetPath.c_str()) == 0;
    bool canReset = e.sc.running ? customized : !e.sc.packaged && (customized || (!e.sc.iconPath.empty() && !usesOwnIcon));
    if (m_cand) {
        if (ui::Button("Discard selection", ICON_X, ui::ButtonKind::Ghost, ImVec2(-1, 0))) {
            m_cand = Candidate();
            m_adjust = IconAdjust();
        }
    } else if (ui::Button(customized ? "Restore original icon" : "Use the app's own icon", ICON_UNDO,
                          ui::ButtonKind::Secondary, ImVec2(-1, 0), canReset)) {
        if (e.sc.running) {
            RestoreRunningApp(e.sc.targetPath);
            ui::Toast(ui::ToastKind::Success, "Restored the original icon of " + U8(e.sc.displayName) + ".");
        } else {
            RestoreOriginal(e.sc.lnkPath);
        }
    }
    if (!canReset && !m_cand)
        ui::Tooltip(e.sc.packaged ? "This pin already shows the app's own icon."
                                  : "This shortcut already uses the app's own icon.");
    ui::EndCard();

    // details
    rise.reset();
    ImGui::Spacing();
    anim::Rise detailsRise(4, 18.0f);
    ui::BeginCard("##details");
    ui::Heading("Details");
    float w = ImGui::GetContentRegionAvail().x;
    auto row = [&](const char* label, const std::string& value) {
        SectionLabel(label);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - S(8));
        ui::TextEllipsis(value.empty() ? "-" : value.c_str(), w, textDim);
    };
    if (e.sc.packaged) row("APP ID", U8(e.sc.aumid));
    else if (e.sc.running) row("STATUS", "Running, not pinned");
    else row("SHORTCUT", U8(FileStem(e.sc.lnkPath)) + ".lnk");
    row("LAUNCHES", e.sc.targetPath.empty() ? "Windows / Store app" : U8(e.sc.targetPath));
    std::wstring iconPath;
    int iconIndex = 0;
    ResolveShortcutIcon(e.sc, iconPath, iconIndex);
    row("ICON FROM", iconPath.empty() ? "App default" : U8(iconPath) + (iconIndex ? "  #" + std::to_string(iconIndex) : ""));
    ImGui::Dummy(ImVec2(0, S(2)));
    if (e.sc.packaged) {
        ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
        ImGui::TextWrapped("Store app: Iconger makes a shortcut with your icon, and you pin it in place of this one.");
        ImGui::PopStyleColor();
    } else if (e.sc.running) {
        ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
        ImGui::TextWrapped("Experimental: the new icon shows on this app's taskbar button, title bar and Alt+Tab while "
                           "Iconger runs. The program itself isn't changed.");
        ImGui::PopStyleColor();
        if (ui::Button("Show program in Explorer", ICON_EXTERNAL, ui::ButtonKind::Secondary, ImVec2(-1, 0)))
            ShowInExplorer(e.sc.targetPath);
    } else if (ui::Button("Show shortcut in Explorer", ICON_EXTERNAL, ui::ButtonKind::Secondary, ImVec2(-1, 0))) {
        ShowInExplorer(e.sc.lnkPath);
    }
    ui::EndCard();
}

void App::DrawAdjustPanel()
{
    ImGui::PushID("adjust");
    // when the first icon is picked, the controls cascade in row by row
    std::optional<anim::Rise> row(std::in_place, m_adjustAt, 0);
    if (!m_cand.brandSvg.empty()) DrawBackgroundControls();
    float x0 = ImGui::GetCursorPosX(), w = ImGui::GetContentRegionAvail().x;
    SectionLabel("ADJUST");
    ImGui::SameLine(x0 + w - S(70));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - S(4));
    bool changed = false;
    if (ui::Button("Reset", nullptr, ui::ButtonKind::Ghost, ImVec2(S(70), S(26)), !m_adjust.IsIdentity())) {
        m_adjust = IconAdjust();
        changed = true;
    }

    // one-click looks; each is just a set of slider values
    struct Preset { const char* name; float hue, sat, bright, contrast; };
    const Preset presets[] = {
        { "Mono",  0, 0, 0, 110 },
        { "Vivid", 0, 150, 0, 112 },
        { "Soft",  0, 60, 12, 88 },
        { "Dark",  0, 90, -35, 115 },
        { "Flip",  180, 100, 0, 100 },
    };
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(9), S(4)));
    ImGui::PushFont(nullptr, fontSmall);
    for (int i = 0; i < (int)std::size(presets); ++i) {
        const Preset& p = presets[i];
        bool on = m_adjust.hue == p.hue && m_adjust.saturation == p.sat && m_adjust.brightness == p.bright &&
                  m_adjust.contrast == p.contrast && m_adjust.tintAmount == 0;
        if (i) ImGui::SameLine(0, S(6));
        if (ui::Button(p.name, nullptr, on ? ui::ButtonKind::Outline : ui::ButtonKind::Secondary)) {
            m_adjust = IconAdjust();
            m_adjust.hue = p.hue; m_adjust.saturation = p.sat;
            m_adjust.brightness = p.bright; m_adjust.contrast = p.contrast;
            changed = true;
        }
    }
    ImGui::PopFont();
    ImGui::PopStyleVar();
    ImGui::Dummy(ImVec2(0, S(2)));

    row.emplace(m_adjustAt, 1);
    changed |= ui::Slider("Hue", &m_adjust.hue, -180, 180, 0, "%+.0f°", ui::SliderTrack::Hue);
    row.emplace(m_adjustAt, 2);
    changed |= ui::Slider("Saturation", &m_adjust.saturation, 0, 200, 100, "%.0f%%");
    row.emplace(m_adjustAt, 3);
    changed |= ui::Slider("Brightness", &m_adjust.brightness, -100, 100, 0, "%+.0f");
    row.emplace(m_adjustAt, 4);
    changed |= ui::Slider("Contrast", &m_adjust.contrast, 0, 200, 100, "%.0f%%");
    row.emplace(m_adjustAt, 5);
    changed |= ui::Slider("Tint", &m_adjust.tintAmount, 0, 100, 0, "%.0f%%");
    row.emplace(m_adjustAt, 6);

    // tint colour: palette swatches + a custom picker
    const ImU32 swatches[] = { primary, IM_COL32(239, 68, 68, 255), IM_COL32(236, 72, 153, 255), IM_COL32(167, 139, 250, 255),
                               IM_COL32(59, 130, 246, 255), IM_COL32(34, 211, 238, 255), IM_COL32(76, 195, 138, 255),
                               IM_COL32(250, 204, 21, 255), IM_COL32(240, 240, 240, 255) };
    float sw = S(24);
    for (int i = 0; i < (int)std::size(swatches); ++i) {
        if (i) ImGui::SameLine(0, S(2));
        ImGui::PushID(i);
        if (ui::Swatch("##sw", swatches[i], m_adjust.tintAmount > 0 && m_adjust.tintColor == swatches[i], sw)) {
            m_adjust.tintColor = swatches[i];
            if (m_adjust.tintAmount == 0) m_adjust.tintAmount = 70; // picking a colour should visibly do something
            changed = true;
        }
        ImGui::PopID();
    }
    ImGui::SameLine(0, S(6));
    ImVec4 custom = ImGui::ColorConvertU32ToFloat4(m_adjust.tintColor);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, sw * 0.5f);
    if (ImGui::ColorEdit3("##tint", &custom.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel |
                                                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_PickerHueWheel)) {
        custom.w = 1;
        m_adjust.tintColor = ImGui::ColorConvertFloat4ToU32(custom);
        if (m_adjust.tintAmount == 0) m_adjust.tintAmount = 70;
        changed = true;
    }
    ImGui::PopStyleVar();
    ui::Tooltip("Custom tint colour");
    row.reset();

    if (changed) RefreshCandidatePreview();
    ImGui::PopID();
}

void App::DrawTaskbarPreview(float width)
{
    // A fake slice of the Windows 11 taskbar at real icon size, with the neighbours.
    float h = S(48), slot = S(44), icon = S(24);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), IM_COL32(32, 32, 32, 255), S(8));

    // only what the taskbar really shows: live pins (plus the edited one if it's a leftover)
    std::vector<int> row;
    for (int i = 0; i < (int)m_entries.size(); ++i)
        if (m_entries[i].sc.onTaskbar || i == m_editing) row.push_back(i);
    int selfPos = (int)(std::find(row.begin(), row.end(), m_editing) - row.begin());
    int visible = std::max(1, std::min((int)row.size(), (int)(width / slot) - 1));
    int first = std::clamp(selfPos - visible / 2, 0, std::max(0, (int)row.size() - visible));
    float x = p.x + (width - visible * slot) * 0.5f;
    for (int k = first; k < first + visible; ++k, x += slot) {
        int i = row[k];
        bool self = i == m_editing;
        const Texture& t = self && m_cand.taskbar ? m_cand.taskbar : m_entries[i].icon;
        ImVec2 s0(x + S(2), p.y + S(4)), s1(x + slot - S(2), p.y + h - S(4));
        if (self) dl->AddRectFilled(s0, s1, IM_COL32(255, 255, 255, 18), S(6));
        ImVec2 ip(x + (slot - icon) * 0.5f, p.y + (h - icon) * 0.5f - S(2));
        if (self) ip.y -= S(5) * Bump(m_candAt, 0.32f); // hops like a clicked taskbar icon
        if (t) dl->AddImage(t.Id(), ip, ImVec2(ip.x + icon, ip.y + icon));
        if (self) dl->AddRectFilled(ImVec2(x + slot * 0.5f - S(8), p.y + h - S(7)),
                                    ImVec2(x + slot * 0.5f + S(8), p.y + h - S(4)), primary, S(2));
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("How it will look on the taskbar");
}

void App::DrawSourceCard()
{
    const Entry& e = m_entries[m_editing];
    float cardH = ImGui::GetContentRegionAvail().y;
    ui::BeginCard("##source", ImVec2(0, cardH));
    ui::Heading("Choose an icon", "Click to preview. Nothing changes until you press Apply.");
    ImGui::Dummy(ImVec2(0, S(2)));

    // segmented control
    struct Tab { SourceTab tab; const char* icon; const char* label; };
    const Tab tabs[] = {
        { SourceTab::ThisApp, ICON_APP_WINDOW, "This app" },
        { SourceTab::File,    ICON_UPLOAD,      "From a file" },
    };
    ImVec2 segP = ImGui::GetCursorScreenPos();
    float segW = ImGui::GetContentRegionAvail().x, segH = S(38);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(segP, ImVec2(segP.x + segW, segP.y + segH), bg, S(10));
    const int tabCount = (int)std::size(tabs);
    float tabW = (segW - S(8)) / tabCount;
    // the selected tab's pill slides to the chosen tab
    int activeTab = 0;
    for (int i = 0; i < tabCount; ++i)
        if (m_tab == tabs[i].tab) activeTab = i;
    float pillX = segP.x + S(4) + tabW * anim::Smooth("##segpill", (float)activeTab, 16.0f);
    ImVec2 pill0(pillX, segP.y + S(4)), pill1(pillX + tabW, segP.y + segH - S(4));
    dl->AddRectFilled(pill0, pill1, card, S(7));
    dl->AddRect(pill0, pill1, border, S(7), S(1));
    for (int i = 0; i < tabCount; ++i) {
        ImGui::SetCursorScreenPos(ImVec2(segP.x + S(4) + tabW * i, segP.y + S(4)));
        bool active = m_tab == tabs[i].tab;
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(7));
        if (ui::Button(tabs[i].label, tabs[i].icon, active ? ui::ButtonKind::Selected : ui::ButtonKind::Ghost,
                       ImVec2(tabW, segH - S(8))))
            m_tab = tabs[i].tab;
        ImGui::PopStyleVar();
    }
    ImGui::SetCursorScreenPos(ImVec2(segP.x, segP.y + segH + S(12)));

    std::wstring wantGrid;
    switch (m_tab) {
    case SourceTab::ThisApp:
        if (!e.sc.targetPath.empty() && CountIcons(e.sc.targetPath) > 0) wantGrid = e.sc.targetPath;
        break;
    case SourceTab::File: {
        // drop zone
        ImVec2 zp = ImGui::GetCursorScreenPos();
        float zw = ImGui::GetContentRegionAvail().x, zh = m_fileLib.empty() ? S(170) : S(96);
        bool clicked = ImGui::InvisibleButton("##drop", ImVec2(zw, zh));
        bool hov = ImGui::IsItemHovered();
        dl->AddRectFilled(zp, ImVec2(zp.x + zw, zp.y + zh), hov ? primarySoft : bg, S(12));
        DashedRect(dl, ImVec2(zp.x + S(1), zp.y + S(1)), ImVec2(zp.x + zw - S(1), zp.y + zh - S(1)),
                   hov ? primary : borderStrong, S(6), S(1.5f));
        const char* l1 = "Drop an image, .ico, .exe or .dll here";
        const char* l2 = "or click to browse  (Ctrl+O)";
        ImVec2 s0 = ImGui::CalcTextSize(ICON_IMAGE_PLUS), s1 = ImGui::CalcTextSize(l1), s2 = ImGui::CalcTextSize(l2);
        float total = s0.y * 1.6f + s1.y + s2.y + S(10);
        float y = zp.y + (zh - total) * 0.5f;
        ImFont* f = ImGui::GetFont();
        dl->AddText(f, ImGui::GetFontSize() * 1.6f, ImVec2(zp.x + (zw - s0.x * 1.6f) * 0.5f, y), primary, ICON_IMAGE_PLUS);
        y += s0.y * 1.6f + S(6);
        dl->AddText(ImVec2(zp.x + (zw - s1.x) * 0.5f, y), text, l1);
        dl->AddText(ImVec2(zp.x + (zw - s2.x) * 0.5f, y + s1.y + S(4)), textSecondary, l2);
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (clicked) BrowseForIcon();

        if (!m_fileLib.empty()) {
            wantGrid = m_fileLib;
            ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
            ImGui::Text("Icons in %s", U8(FileStem(m_fileLib) + LowerExt(m_fileLib)).c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
            ImGui::TextWrapped("PNG and JPG images are padded to a square and saved as a multi-size .ico in your "
                               "Iconger folder, so the icon stays sharp and keeps working if you delete the original.");
            ImGui::PopStyleColor();
        }
        break;
    }
    }

    if (wantGrid.empty()) m_grid.Clear();
    else if (_wcsicmp(wantGrid.c_str(), m_grid.path.c_str()) != 0) m_grid.Open(wantGrid);
    if (m_tab == SourceTab::ThisApp) DrawThisAppTab(ImGui::GetContentRegionAvail().y);
    else if (!m_grid.path.empty()) DrawIconGrid();
    ui::EndCard();
}

// ============================================================================
// "This app": built-in icons + online icon libraries
// ============================================================================

void App::SearchLibraries()
{
    if (m_editing < 0 || !m_settings.onlineLibraries) return;
    std::vector<std::string> queries = { m_libQuery };
    // for the default query also try the exe name ("Code.exe" for Visual Studio Code)
    const PinnedShortcut& sc = m_entries[m_editing].sc;
    if (queries[0] == U8(sc.displayName) && !sc.targetPath.empty()) queries.push_back(U8(FileStem(sc.targetPath)));
    m_library.Search(queries);
}

int App::DrawTiles(const char* id, int count, const std::function<const Texture*(int)>& tex,
                   const std::function<bool(int)>& selected, const std::function<bool(int)>& loading,
                   const std::function<std::string(int)>& tooltip)
{
    float cell = S(56), gap = S(4), ic = S(32);
    int cols = std::max(1, (int)((ImGui::GetContentRegionAvail().x + gap) / (cell + gap)));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    int clicked = -1;
    float pulse = 0.55f + 0.45f * std::sin((float)ImGui::GetTime() * 5.0f);
    ImGui::PushID(id);
    for (int i = 0; i < count; ++i) {
        if (i % cols) ImGui::SameLine(0, gap);
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool pressed = ImGui::InvisibleButton("##t", ImVec2(cell, cell));
        bool hov = ImGui::IsItemHovered();
        ImGuiID tid = ImGui::GetItemID();
        ImGui::PopID();
        if (!ImGui::IsItemVisible()) continue;
        ImVec2 q(p.x + cell, p.y + cell);
        const Texture* t = tex(i);
        const bool ready = t && *t;
        // hover and selection glide; a thumbnail fades and grows in when it arrives
        const float h = anim::Smooth(tid, hov ? 1.0f : 0.0f, 18.0f);
        const float sel = anim::Smooth(tid + 1, selected(i) ? 1.0f : 0.0f, 16.0f);
        const float in = anim::Smooth(tid + 2, ready ? 1.0f : 0.0f, 10.0f, 0.0f);
        if (h > 0 && sel < 1) dl->AddRectFilled(p, q, FadeCol(cardHover, h * (1 - sel)), S(8));
        if (sel > 0) {
            float g = S(3) * (1 - sel); // the outline swells in from slightly outside
            dl->AddRectFilled(p, q, FadeCol(primarySoft, sel), S(8));
            dl->AddRect(ImVec2(p.x - g, p.y - g), ImVec2(q.x + g, q.y + g), FadeCol(primary, sel), S(8) + g, S(1.5f));
        }
        ImVec2 ip(p.x + (cell - ic) * 0.5f, p.y + (cell - ic) * 0.5f);
        if (ready) {
            anim::Block b = anim::Begin();
            dl->AddImage(t->Id(), ip, ImVec2(ip.x + ic, ip.y + ic));
            anim::End(b, in, ImVec2(0, 0), (0.85f + 0.15f * in) * (1.0f + 0.1f * h),
                      ImVec2(ip.x + ic * 0.5f, ip.y + ic * 0.5f));
        } else if (loading(i)) {
            ImVec4 c = ImGui::ColorConvertU32ToFloat4(accentBg);
            c.w *= pulse;
            dl->AddRectFilled(ip, ImVec2(ip.x + ic, ip.y + ic), ImGui::ColorConvertFloat4ToU32(c), S(8));
            ui::KeepAnimating(0.1f);
        } else {
            ImVec2 xs = ImGui::CalcTextSize(ICON_X);
            dl->AddText(ImVec2(p.x + (cell - xs.x) * 0.5f, p.y + (cell - xs.y) * 0.5f), textMuted, ICON_X);
        }
        if (hov) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", tooltip(i).c_str());
        }
        if (pressed && t && *t) clicked = i;
    }
    ImGui::PopID();
    return clicked;
}

void App::DrawThisAppTab(float height)
{
    const PinnedShortcut& sc = m_entries[m_editing].sc;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(14), S(12)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(10));
    ImGui::BeginChild("##thisapp", ImVec2(0, height), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // 1) icons shipped inside the program
    if (!m_grid.path.empty()) {
        m_grid.Pump(24, (int)std::lround(S(32)));
        std::string label = "BUILT INTO " + U8(sc.displayName);
        for (auto& c : label) c = (char)toupper((unsigned char)c);
        SectionLabel(label.c_str());
        bool sameFile = m_cand && _wcsicmp(m_cand.path.c_str(), m_grid.path.c_str()) == 0;
        int shown = std::min(m_grid.count, 240);
        int hit = DrawTiles("builtin", shown,
            [&](int i) -> const Texture* { return i < m_grid.loaded ? &m_grid.textures[i] : nullptr; },
            [&](int i) { return sameFile && m_cand.index == i; },
            [&](int i) { return i >= m_grid.loaded; },
            [&](int i) { return "Icon #" + std::to_string(i) + " in " + U8(FileStem(m_grid.path) + LowerExt(m_grid.path)); });
        if (hit >= 0) SetCandidate(m_grid.path, hit, U8(FileStem(m_grid.path)) + " #" + std::to_string(hit));
        ImGui::Dummy(ImVec2(0, S(10)));
    }

    // 2) matches from the online libraries
    SectionLabel("ICON LIBRARIES");
    if (!m_settings.onlineLibraries) {
        ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
        ImGui::TextWrapped("Online icon libraries are turned off. You can turn them back on in Settings.");
        ImGui::PopStyleColor();
    } else {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##libq", ICON_SEARCH "  Search thousands of app icons", m_libQuery, sizeof(m_libQuery)))
            m_libEditTime = ImGui::GetTime();
        ImGui::Dummy(ImVec2(0, S(2)));

        const auto& tiles = m_library.Tiles();
        if (m_library.IndexLoading()) {
            ui::Spinner(S(18), primary);
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
            ImGui::TextUnformatted("Loading icon libraries...");
            ImGui::PopStyleColor();
        } else if (!m_library.HasIndex()) {
            ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
            ImGui::TextWrapped("Couldn't load the icon libraries (%s). Check your internet connection.", m_library.IndexError().c_str());
            ImGui::PopStyleColor();
            if (ui::Button("Try again", ICON_REFRESH, ui::ButtonKind::Secondary)) m_library.EnsureIndex();
        } else if (m_library.NothingToShow()) {
            ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
            ImGui::TextWrapped("No icons match \"%s\". Try a shorter name, or a word like \"notes\" or \"music\".", m_libQuery);
            ImGui::PopStyleColor();
        } else {
            std::vector<int> shown; // failed downloads and repeats of the same picture are hidden
            for (int i = 0; i < (int)tiles.size(); ++i)
                if (!tiles[i].failed && !tiles[i].duplicate) shown.push_back(i);
            int hit = DrawTiles("online", (int)shown.size(),
                [&](int k) -> const Texture* { return &tiles[shown[k]].tex; },
                [&](int k) { return m_cand && m_cand.path.empty() && m_cand.id == tiles[shown[k]].icon.Id(); },
                [&](int k) { return tiles[shown[k]].loading; },
                [&](int k) {
                    const LibraryIcon& ic = tiles[shown[k]].icon;
                    return std::string(LibraryName(ic.lib)) + ": " + (ic.title.empty() ? ic.name : ic.title);
                });
            if (hit >= 0) {
                // the file is cached by now, so a full-size render is quick
                const LibraryIcon& ic = tiles[shown[hit]].icon;
                Image master;
                std::string err;
                if (FetchLibraryIcon(ic, 256, master, err)) {
                    SetCandidatePixels(std::move(master), ic.Id(), std::string(LibraryName(ic.lib)) + ": " + ic.name);
                    // Simple Icons: keep the logo, so its background can be changed
                    std::string svg;
                    if (ic.lib == IconLibrary::SimpleIcons && m_cand && FetchLibrarySvg(ic, svg, err)) {
                        m_cand.brandSvg = std::move(svg);
                        m_cand.tileColor = m_cand.brandColor = ic.brandRgb;
                    }
                } else
                    ui::Toast(ui::ToastKind::Error, "Couldn't load that icon: " + err);
            }
        }
        ImGui::Dummy(ImVec2(0, S(6)));
        ImGui::PushFont(nullptr, fontSmall);
        ImGui::PushStyleColor(ImGuiCol_Text, textMuted);
        ImGui::TextWrapped("From Dashboard Icons (Apache-2.0), the WhiteSur, Fluent, Papirus, Tela and Candy icon "
                           "themes (GPL-3.0) and Simple Icons (CC0). App logos belong to their owners.");
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImGui::EndChild();
}


void App::DrawIconGrid()
{
    m_grid.Pump(24, (int)std::lround(S(32)));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(8), S(8)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(10));
    ImGui::BeginChild("##icons", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    float cell = S(56), gap = S(4);
    float avail = ImGui::GetContentRegionAvail().x;
    int cols = std::max(1, (int)((avail + gap) / (cell + gap)));
    int rows = (m_grid.count + cols - 1) / cols;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool sameFile = m_cand && _wcsicmp(m_cand.path.c_str(), m_grid.path.c_str()) == 0;

    ImGuiListClipper clipper;
    clipper.Begin(rows, cell + gap);
    while (clipper.Step()) {
        for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
            for (int c = 0; c < cols; ++c) {
                int i = r * cols + c;
                if (i >= m_grid.count) break;
                if (c) ImGui::SameLine(0, gap);
                ImGui::PushID(i);
                ImVec2 p = ImGui::GetCursorScreenPos();
                bool clicked = ImGui::InvisibleButton("##ic", ImVec2(cell, cell));
                bool hov = ImGui::IsItemHovered();
                ImGui::PopID();
                bool sel = sameFile && m_cand.index == i;
                ImVec2 q(p.x + cell, p.y + cell);
                if (sel) {
                    dl->AddRectFilled(p, q, primarySoft, S(8));
                    dl->AddRect(p, q, primary, S(8), S(1.5f));
                } else if (hov) {
                    dl->AddRectFilled(p, q, cardHover, S(8));
                }
                float ic = S(32);
                ImVec2 ip(p.x + (cell - ic) * 0.5f, p.y + (cell - ic) * 0.5f);
                if (i < m_grid.loaded && m_grid.textures[i])
                    dl->AddImage(m_grid.textures[i].Id(), ip, ImVec2(ip.x + ic, ip.y + ic));
                else if (i >= m_grid.loaded)
                    dl->AddRectFilled(ip, ImVec2(ip.x + ic, ip.y + ic), card, S(6));
                if (hov && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Icon #%d", i);
                if (clicked && i < m_grid.loaded && m_grid.textures[i])
                    SetCandidate(m_grid.path, i, U8(FileStem(m_grid.path)) + " #" + std::to_string(i));
            }
        }
    }
    ImGui::EndChild();
}

// Simple Icons are a logo on a tile we draw ourselves, so its shape and colour are ours
// to change. (Tint can't make a black tile blue: it keeps the tile's own brightness.)
void App::DrawBackgroundControls()
{
    ImGui::PushID("background");
    SectionLabel("BACKGROUND");
    bool changed = false;
    struct Shape { TileShape shape; const char* label; };
    const Shape shapes[] = { { TileShape::RoundedSquare, "Square" }, { TileShape::Circle, "Circle" }, { TileShape::None, "None" } };
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(9), S(4)));
    ImGui::PushFont(nullptr, fontSmall);
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine(0, S(6));
        bool on = m_cand.tileShape == shapes[i].shape;
        if (ui::Button(shapes[i].label, nullptr, on ? ui::ButtonKind::Outline : ui::ButtonKind::Secondary)) {
            m_cand.tileShape = shapes[i].shape;
            changed = true;
        }
    }
    ImGui::PopFont();
    ImGui::PopStyleVar();
    if (m_cand.tileShape == TileShape::None) {
        ImGui::PushStyleColor(ImGuiCol_Text, textMuted);
        ImGui::PushFont(nullptr, fontSmall);
        ImGui::TextUnformatted("No background: the colour below is the logo's.");
        ImGui::PopFont();
        ImGui::PopStyleColor();
    }

    // colour: the brand's, a few useful ones, or any via the picker
    auto toCol = [](uint32_t rgb) { return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255); };
    const uint32_t colours[] = { m_cand.brandColor, 0x000000, 0xFFFFFF, 0x3B82F6, 0x22D3EE, 0x4CC38A,
                                 0xFACC15, 0xFF8A3D, 0xEF4444, 0xEC4899, 0xA78BFA };
    const float sw = S(24);
    for (int i = 0; i < (int)std::size(colours); ++i) {
        if (i) ImGui::SameLine(0, S(2));
        ImGui::PushID(i);
        if (ui::Swatch("##c", toCol(colours[i]), m_cand.tileColor == colours[i], sw)) {
            m_cand.tileColor = colours[i];
            changed = true;
        }
        if (i == 0) ui::Tooltip("Brand colour");
        ImGui::PopID();
    }
    ImGui::SameLine(0, S(6));
    ImVec4 custom((float)((m_cand.tileColor >> 16) & 0xFF) / 255, (float)((m_cand.tileColor >> 8) & 0xFF) / 255,
                  (float)(m_cand.tileColor & 0xFF) / 255, 1);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, sw * 0.5f);
    if (ImGui::ColorEdit3("##custom", &custom.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel |
                                                 ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_PickerHueWheel)) {
        m_cand.tileColor = (uint32_t)std::lround(custom.x * 255) << 16 | (uint32_t)std::lround(custom.y * 255) << 8 |
                           (uint32_t)std::lround(custom.z * 255);
        changed = true;
    }
    ImGui::PopStyleVar();
    ui::Tooltip("Any colour");
    ImGui::Dummy(ImVec2(0, S(4)));
    if (changed) RedrawBrandTile();
    ImGui::PopID();
}
