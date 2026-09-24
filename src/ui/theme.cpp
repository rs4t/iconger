#include "ui/theme.h"
#include "app_paths.h"
#include <windows.h>

namespace theme {

Fonts fonts;
static float g_scale = 1.0f;

float Scale() { return g_scale; }

static ImVec4 V(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

static void MergeIcons(ImFontAtlas* atlas)
{
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(1001), RT_RCDATA);
    HGLOBAL mem = res ? LoadResource(nullptr, res) : nullptr;
    void* data = mem ? LockResource(mem) : nullptr;
    if (!data) return;
    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.FontDataOwnedByAtlas = false; // resource memory lives as long as the exe
    cfg.GlyphOffset = ImVec2(0, 2.0f);
    cfg.GlyphMinAdvanceX = fontBody;
    atlas->AddFontFromMemoryTTF(data, (int)SizeofResource(nullptr, res), fontBody, &cfg);
}

static void MergeCjkFallback(ImFontAtlas* atlas)
{
    // Pinned app names can be Chinese/Japanese; Segoe UI has no glyphs for them.
    // The 1.92 atlas only rasterizes glyphs that are actually drawn.
    // The file is ~20 MB, so read it once and share it between faces.
    static std::vector<uint8_t> data;
    if (data.empty() && !ReadWholeFile(ExpandEnv(L"%SystemRoot%\\Fonts\\msyh.ttc"), data)) return;
    if (data.empty()) return;
    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.FontDataOwnedByAtlas = false;
    atlas->AddFontFromMemoryTTF(data.data(), (int)data.size(), fontBody, &cfg);
}

static ImFont* AddFace(ImFontAtlas* atlas, const wchar_t* file, bool withFallback)
{
    std::wstring path = ExpandEnv(std::wstring(L"%SystemRoot%\\Fonts\\") + file);
    ImFont* f = FileExists(path) ? atlas->AddFontFromFileTTF(WideToUtf8(path).c_str(), fontBody) : nullptr;
    if (!f) f = atlas->AddFontDefault();
    MergeIcons(atlas);
    if (withFallback) MergeCjkFallback(atlas);
    return f;
}

void LoadFonts()
{
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    fonts.regular  = AddFace(atlas, L"segoeui.ttf", true);
    fonts.semibold = AddFace(atlas, L"seguisb.ttf", true);
    fonts.bold     = AddFace(atlas, L"segoeuib.ttf", false);
    ImGui::GetIO().FontDefault = fonts.regular;
}

void ApplyStyle(float dpiScale)
{
    g_scale = dpiScale;
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();

    s.WindowPadding     = ImVec2(0, 0);
    s.WindowRounding    = 0;
    s.WindowBorderSize  = 0;
    s.ChildRounding     = 12;
    s.ChildBorderSize   = 1;
    s.FramePadding      = ImVec2(12, 8);
    s.FrameRounding     = 8;
    s.FrameBorderSize   = 1;
    s.ItemSpacing       = ImVec2(10, 10);
    s.ItemInnerSpacing  = ImVec2(8, 6);
    s.ScrollbarSize     = 10;
    s.ScrollbarRounding = 6;
    s.GrabRounding      = 6;
    s.PopupRounding     = 12;
    s.PopupBorderSize   = 1;
    s.WindowTitleAlign  = ImVec2(0.5f, 0.5f);
    s.ButtonTextAlign   = ImVec2(0.5f, 0.5f);
    s.FontSizeBase      = fontBody;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                 = V(text);
    c[ImGuiCol_TextDisabled]         = V(textMuted);
    c[ImGuiCol_WindowBg]             = V(bg);
    c[ImGuiCol_ChildBg]              = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]              = V(card);
    c[ImGuiCol_Border]               = V(border);
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]              = V(bg);
    c[ImGuiCol_FrameBgHovered]       = V(card);
    c[ImGuiCol_FrameBgActive]        = V(card);
    c[ImGuiCol_Button]               = V(card);
    c[ImGuiCol_ButtonHovered]        = V(accentBg);
    c[ImGuiCol_ButtonActive]         = V(borderStrong);
    c[ImGuiCol_Header]               = V(accentBg);
    c[ImGuiCol_HeaderHovered]        = V(accentBg);
    c[ImGuiCol_HeaderActive]         = V(borderStrong);
    c[ImGuiCol_CheckMark]            = V(primary);
    c[ImGuiCol_SliderGrab]           = V(primary);
    c[ImGuiCol_SliderGrabActive]     = V(primaryHover);
    c[ImGuiCol_Separator]            = V(border);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]        = V(accentBg);
    c[ImGuiCol_ScrollbarGrabHovered] = V(borderStrong);
    c[ImGuiCol_ScrollbarGrabActive]  = V(textMuted);
    c[ImGuiCol_TextSelectedBg]       = V(primarySoft);
    c[ImGuiCol_NavCursor]            = V(primary);
    c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.02f, 0.03f, 0.06f, 0.70f);

    s.ScaleAllSizes(dpiScale);
    s.FontScaleDpi = dpiScale;
}

} // namespace theme
