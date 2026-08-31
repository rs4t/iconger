#pragma once
#include <imgui.h>

namespace ui {

inline constexpr ImU32 c_header_bg      = IM_COL32(28, 28, 30, 255);
inline constexpr ImU32 c_panel_bg       = IM_COL32(32, 32, 34, 255);
inline constexpr ImU32 c_panel_border   = IM_COL32(48, 48, 52, 255);
inline constexpr ImU32 c_card_bg        = IM_COL32(38, 38, 42, 255);
inline constexpr ImU32 c_card_hover     = IM_COL32(45, 45, 50, 255);
inline constexpr ImU32 c_card_active    = IM_COL32(55, 55, 62, 255);
inline constexpr ImU32 c_accent         = IM_COL32(86, 140, 220, 255);
inline constexpr ImU32 c_accent_hover   = IM_COL32(100, 160, 240, 255);
inline constexpr ImU32 c_accent_dim     = IM_COL32(86, 140, 220, 60);
inline constexpr ImU32 c_text           = IM_COL32(220, 220, 225, 255);
inline constexpr ImU32 c_text_dim       = IM_COL32(150, 150, 158, 255);
inline constexpr ImU32 c_text_muted     = IM_COL32(100, 100, 110, 255);
inline constexpr ImU32 c_text_bright    = IM_COL32(255, 255, 255, 255);
inline constexpr ImU32 c_positive       = IM_COL32(80, 200, 120, 255);
inline constexpr ImU32 c_warning        = IM_COL32(255, 180, 60, 255);
inline constexpr ImU32 c_danger         = IM_COL32(220, 80, 80, 255);
inline constexpr ImU32 c_separator      = IM_COL32(48, 48, 52, 255);
inline constexpr ImU32 c_popup_bg       = IM_COL32(38, 38, 42, 250);
inline constexpr ImU32 c_log_bg         = IM_COL32(22, 22, 24, 220);

inline constexpr float  c_rounding      = 8.0f;
inline constexpr float  c_rounding_sm   = 6.0f;
inline constexpr float  c_rounding_xs   = 4.0f;
inline constexpr float  c_frame_rounding= 6.0f;
inline constexpr float  c_window_rounding = 10.0f;

namespace icons {
inline constexpr char   refresh_icon[]  = "\xEF\x80\xA1";
inline constexpr char   folder_icon[]   = "\xEF\x81\xBB";
inline constexpr char   image_icon[]    = "\xEF\x80\xB5";
inline constexpr char   apply_icon[]    = "\xEF\x80\x8C";
inline constexpr char   reset_icon[]    = "\xEF\x80\xA9";
inline constexpr char   list_icon[]     = "\xEF\x80\xBA";
inline constexpr char   preview_icon[]  = "\xEF\x80\xAE";
inline constexpr char   log_icon[]      = "\xEF\x80\x91";
inline constexpr char   info_icon[]     = "\xEF\x81\x9A";
inline constexpr char   warning_icon[]  = "\xEF\x81\xB1";
inline constexpr char   error_icon[]    = "\xEF\x81\x97";
inline constexpr char   check_icon[]    = "\xEF\x80\x8C";
inline constexpr char   search_icon[]   = "\xEF\x80\x82";
inline constexpr char   close_icon[]    = "\xEF\x80\x8D";
inline constexpr char   settings_icon[] = "\xEF\x80\x93";
inline constexpr char   exe_icon[]      = "\xEF\xA1\xB1";
inline constexpr char   png_icon[]      = "\xEF\x87\x87";
inline constexpr char   target_icon[]   = "\xEF\x84\x83";
}

inline void SetupStyle()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowPadding = ImVec2(0, 0);
    s.WindowRounding = c_window_rounding;
    s.WindowBorderSize = 0.0f;
    s.ChildRounding = c_rounding;
    s.ChildBorderSize = 0.0f;
    s.FramePadding = ImVec2(10, 6);
    s.FrameRounding = c_frame_rounding;
    s.ItemSpacing = ImVec2(8, 8);
    s.ScrollbarSize = 8.0f;
    s.ScrollbarRounding = 4.0f;
    s.GrabRounding = 4.0f;
    s.PopupRounding = c_rounding;
    s.PopupBorderSize = 0.0f;
    s.ButtonTextAlign = ImVec2(0.5f, 0.5f);

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]           = ImVec4(0.12f, 0.12f, 0.13f, 1.00f);
    c[ImGuiCol_ChildBg]            = ImVec4(0.13f, 0.13f, 0.14f, 1.00f);
    c[ImGuiCol_PopupBg]            = ImVec4(0.15f, 0.15f, 0.16f, 0.98f);
    c[ImGuiCol_FrameBg]            = ImVec4(0.15f, 0.15f, 0.16f, 1.00f);
    c[ImGuiCol_FrameBgHovered]     = ImVec4(0.18f, 0.18f, 0.20f, 1.00f);
    c[ImGuiCol_FrameBgActive]      = ImVec4(0.22f, 0.22f, 0.24f, 1.00f);
    c[ImGuiCol_Button]             = ImVec4(0.34f, 0.55f, 0.86f, 0.60f);
    c[ImGuiCol_ButtonHovered]      = ImVec4(0.39f, 0.63f, 0.94f, 0.80f);
    c[ImGuiCol_ButtonActive]       = ImVec4(0.28f, 0.47f, 0.78f, 1.00f);
    c[ImGuiCol_Header]             = ImVec4(0.34f, 0.55f, 0.86f, 0.30f);
    c[ImGuiCol_HeaderHovered]      = ImVec4(0.34f, 0.55f, 0.86f, 0.50f);
    c[ImGuiCol_HeaderActive]       = ImVec4(0.34f, 0.55f, 0.86f, 0.70f);
    c[ImGuiCol_Separator]          = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
    c[ImGuiCol_Text]               = ImVec4(0.86f, 0.86f, 0.88f, 1.00f);
    c[ImGuiCol_TextDisabled]       = ImVec4(0.39f, 0.39f, 0.43f, 1.00f);
    c[ImGuiCol_TextSelectedBg]     = ImVec4(0.34f, 0.55f, 0.86f, 0.35f);
    c[ImGuiCol_ModalWindowDimBg]   = ImVec4(0.00f, 0.00f, 0.00f, 0.35f);
    c[ImGuiCol_NavHighlight]       = ImVec4(0.34f, 0.55f, 0.86f, 0.80f);
}

} // namespace ui
