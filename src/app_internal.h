#pragma once
// Shared by the app_*.cpp files: small UI helpers and includes. Not a public API.
#include "app.h"
#include "app_paths.h"
#include "icon_utils.h"
#include "ui/anim.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace appui {

using namespace theme;

inline ImU32 FadeCol(ImU32 c, float a)
{
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.w *= a;
    return ImGui::ColorConvertFloat4ToU32(v);
}

inline ImU32 MixCol(ImU32 a, ImU32 b, float t)
{
    if (t <= 0) return a;
    if (t >= 1) return b;
    return ImGui::ColorConvertFloat4ToU32(ImLerp(ImGui::ColorConvertU32ToFloat4(a), ImGui::ColorConvertU32ToFloat4(b), t));
}

/// 0 -> 1 -> 0 over `duration` seconds after `start`: a one-off pop or pulse.
inline float Bump(double start, float duration)
{
    float t = (float)(ImGui::GetTime() - start) / duration;
    if (t < 0 || t >= 1 || !anim::Enabled()) return 0.0f;
    ui::KeepAnimating(0.05f);
    return std::sin(t * IM_PI);
}

inline constexpr const char* kRepoUrl = "https://github.com/rs4t/iconger";

inline std::string U8(const std::wstring& w) { return WideToUtf8(w); }

inline bool ContainsNoCase(const std::string& hay, const char* needle)
{
    if (!*needle) return true;
    std::wstring h = Utf8ToWide(hay), n = Utf8ToWide(needle);
    return FindNLSStringEx(LOCALE_NAME_USER_DEFAULT, FIND_FROMSTART | LINGUISTIC_IGNORECASE,
                           h.c_str(), (int)h.size(), n.c_str(), (int)n.size(),
                           nullptr, nullptr, nullptr, 0) >= 0;
}

inline void ShowInExplorer(const std::wstring& file)
{
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(file.c_str());
    if (pidl) {
        SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
        ILFree(pidl);
    }
}

inline void OpenPath(const std::wstring& path)
{
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

inline void DashedRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float dash, float thickness)
{
    auto line = [&](ImVec2 p, ImVec2 q) {
        float len = std::hypot(q.x - p.x, q.y - p.y);
        ImVec2 d((q.x - p.x) / len, (q.y - p.y) / len);
        for (float t = 0; t < len; t += dash * 2) {
            float e = std::min(t + dash, len);
            dl->AddLine(ImVec2(p.x + d.x * t, p.y + d.y * t), ImVec2(p.x + d.x * e, p.y + d.y * e), col, thickness);
        }
    };
    line(a, ImVec2(b.x, a.y));
    line(ImVec2(b.x, a.y), b);
    line(b, ImVec2(a.x, b.y));
    line(ImVec2(a.x, b.y), a);
}

inline void DrawTexture(const Texture& tex, float size, float rounding = 0)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    if (tex)
        ImGui::GetWindowDrawList()->AddImageRounded(tex.Id(), p, ImVec2(p.x + size, p.y + size),
                                                    ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, rounding);
}

// Framed square holding an icon (the "before/after" tiles).
inline void IconFrame(const Texture& tex, float size, const char* caption, bool highlight)
{
    ImGui::BeginGroup();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size), bg, S(14));
    dl->AddRect(p, ImVec2(p.x + size, p.y + size), highlight ? primary : border, S(14), S(highlight ? 1.5f : 1));
    float inner = std::floor(size * 0.6f);
    ImVec2 ip(p.x + (size - inner) * 0.5f, p.y + (size - inner) * 0.5f);
    if (tex) {
        dl->AddImage(tex.Id(), ip, ImVec2(ip.x + inner, ip.y + inner));
    } else {
        ImVec2 ts = ImGui::CalcTextSize(ICON_IMAGE_PLUS);
        dl->AddText(ImVec2(p.x + (size - ts.x) * 0.5f, p.y + (size - ts.y) * 0.5f), textMuted, ICON_IMAGE_PLUS);
    }
    ImVec2 cs = ImGui::CalcTextSize(caption);
    ImGui::SetCursorScreenPos(ImVec2(p.x + (size - cs.x) * 0.5f, p.y + size + S(6)));
    ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
    ImGui::TextUnformatted(caption);
    ImGui::PopStyleColor();
    ImGui::EndGroup();
}

inline std::wstring SafeFileName(std::wstring name)
{
    for (auto& c : name)
        if (wcschr(L"<>:\"/\\|?*", c) || c < 32) c = L'_';
    return name;
}

// Full path of the running iconger.exe.
inline std::wstring ExePath()
{
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
    return std::wstring(buf, n);
}

// Shell item to ask for a pin's icon: the .lnk, or the packaged app itself.
inline std::wstring ShellPath(const PinnedShortcut& sc) { return sc.packaged ? AppsFolderPath(sc.aumid) : sc.lnkPath; }

// The shortcut Iconger made for a packaged app that this pin was created from
// (same name, same app ID), or empty.
inline std::wstring IcongerAppShortcut(const PinnedShortcut& sc)
{
    if (sc.packaged || sc.aumid.empty() || sc.lnkPath.empty()) return {};
    std::wstring ours = AppShortcutsFolder(false) + L"\\" + SafeFileName(sc.displayName) + L".lnk";
    PinnedShortcut made;
    if (!ReadShortcut(ours, made) || _wcsicmp(made.aumid.c_str(), sc.aumid.c_str()) != 0) return {};
    return ours;
}

// Stable identity of a pin across reloads.
inline std::wstring EntryKey(const PinnedShortcut& sc) { return sc.packaged ? sc.aumid : sc.lnkPath; }

// Right edge of the content region in window coordinates (GetContentRegionMax is gone in 1.92).
inline float RightEdge() { return ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x; }

inline void SectionLabel(const char* label)
{
    ImGui::PushFont(fonts.semibold, fontSmall);
    ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

} // namespace appui
