#include "app.h"
#include "app_paths.h"
#include "icon_utils.h"
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

using namespace theme;
using Microsoft::WRL::ComPtr;

namespace {

constexpr const char* kRepoUrl = "https://github.com/rs4t/iconger";

std::string U8(const std::wstring& w) { return WideToUtf8(w); }

bool ContainsNoCase(const std::string& hay, const char* needle)
{
    if (!*needle) return true;
    std::wstring h = Utf8ToWide(hay), n = Utf8ToWide(needle);
    return FindNLSStringEx(LOCALE_NAME_USER_DEFAULT, FIND_FROMSTART | LINGUISTIC_IGNORECASE,
                           h.c_str(), (int)h.size(), n.c_str(), (int)n.size(),
                           nullptr, nullptr, nullptr, 0) >= 0;
}

void ShowInExplorer(const std::wstring& file)
{
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(file.c_str());
    if (pidl) {
        SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
        ILFree(pidl);
    }
}

void OpenPath(const std::wstring& path)
{
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void DashedRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float dash, float thickness)
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

void DrawTexture(const Texture& tex, float size, float rounding = 0)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    if (tex)
        ImGui::GetWindowDrawList()->AddImageRounded(tex.Id(), p, ImVec2(p.x + size, p.y + size),
                                                    ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, rounding);
}

// Framed square holding an icon (the "before/after" tiles).
void IconFrame(const Texture& tex, float size, const char* caption, bool highlight)
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

// Right edge of the content region in window coordinates (GetContentRegionMax is gone in 1.92).
float RightEdge() { return ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x; }

void SectionLabel(const char* label)
{
    ImGui::PushFont(fonts.semibold, fontSmall);
    ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

} // namespace

// ============================================================================
// Lifecycle / data
// ============================================================================

void App::Init(HWND hwnd, float dpiScale)
{
    m_hwnd = hwnd;
    m_settings.Load();
    m_backup.Load();
    ApplyStyle(dpiScale);
    Reload();
}

void App::Shutdown()
{
    if (m_restartThread.joinable()) m_restartThread.join();
    m_entries.clear();
    m_editingPreview.Reset();
    m_cand = Candidate();
    m_adjust = IconAdjust();
    m_grid.Clear();
    m_online.clear();
    ++m_onlineGen;
}

void App::SetDpiScale(float scale)
{
    ApplyStyle(scale);
    // textures were extracted at pixel size for the old scale
    for (auto& e : m_entries) e.icon = LoadEntryIcon(e.sc, S(40));
    if (m_editing >= 0) m_editingPreview = LoadEntryIcon(m_entries[m_editing].sc, S(96));
    if (m_cand) { // re-render at the new pixel sizes, keeping the adjustments
        m_cand.base96 = CandidateImage((int)std::lround(S(96)));
        m_cand.base24 = CandidateImage((int)std::lround(S(24)));
        RefreshCandidatePreview();
    }
    if (!m_grid.path.empty()) m_grid.Open(m_grid.path);
    m_libQueryRan.clear(); // thumbnails too
    RunLibrarySearch();
}

Texture App::LoadEntryIcon(const PinnedShortcut& sc, float px) const
{
    int size = std::max(16, (int)std::lround(px));
    std::wstring path;
    int index = 0;
    ResolveShortcutIcon(sc, path, index);
    Image img;
    // Our own extraction first: the shell's icon cache would show the old icon
    // until Explorer restarts. Fall back to the shell for Store apps etc.
    if (!LoadIconImage(path, index, size, img))
        LoadShellItemImage(sc.lnkPath, size, img);
    return Texture(img);
}

void App::Reload()
{
    std::wstring keepLnk = m_editing >= 0 ? m_entries[m_editing].sc.lnkPath : L"";
    m_entries.clear();
    for (auto& sc : EnumeratePinnedShortcuts()) {
        Entry e;
        e.sc = std::move(sc);
        e.icon = LoadEntryIcon(e.sc, S(40));
        m_entries.push_back(std::move(e));
    }
    m_editing = -1;
    if (!keepLnk.empty()) {
        for (int i = 0; i < (int)m_entries.size(); ++i)
            if (_wcsicmp(m_entries[i].sc.lnkPath.c_str(), keepLnk.c_str()) == 0) m_editing = i;
        if (m_editing < 0) CloseEditor(); // it was unpinned meanwhile
    }
}

void App::ReloadEntry(Entry& e)
{
    PinnedShortcut fresh;
    if (ReadShortcut(e.sc.lnkPath, fresh)) e.sc = std::move(fresh);
    e.icon = LoadEntryIcon(e.sc, S(40));
    if (m_editing >= 0 && &m_entries[m_editing] == &e) m_editingPreview = LoadEntryIcon(e.sc, S(96));
}

bool App::IsCustomized(const Entry& e) const { return m_backup.Has(e.sc.lnkPath); }

bool App::IsBusy() const
{
    return m_restarting || m_grid.loaded < m_grid.count || ui::IsAnimating() || m_jobs.Busy();
}

// ============================================================================
// Actions
// ============================================================================

void App::OpenEditor(int index)
{
    m_editing = index;
    m_editingPreview = LoadEntryIcon(m_entries[index].sc, S(96));
    m_cand = Candidate();
    m_adjust = IconAdjust();
    m_fileLib.clear();
    m_grid.Clear();
    const auto& sc = m_entries[index].sc;
    // "This app" has the app's own icons plus library matches; without either, start on files
    bool ownIcons = !sc.targetPath.empty() && CountIcons(sc.targetPath) > 1;
    m_tab = ownIcons || m_settings.onlineLibraries ? SourceTab::ThisApp : SourceTab::File;
    strncpy_s(m_libQuery, WideToUtf8(sc.displayName).c_str(), _TRUNCATE);
    m_libQueryRan.clear();
    m_online.clear();
    ++m_onlineGen;
    EnsureIndex();
    RunLibrarySearch();
}

void App::CloseEditor()
{
    m_editing = -1;
    m_editingPreview.Reset();
    m_cand = Candidate();
    m_adjust = IconAdjust();
    m_grid.Clear();
    m_online.clear();
    ++m_onlineGen;
}

void App::SetCandidate(const std::wstring& path, int index, const std::string& label)
{
    Candidate c;
    c.path = path;
    c.index = index;
    c.label = label;
    c.id = WideToUtf8(FileStem(path)) + "-" + std::to_string(index);
    if (!LoadIconImage(path, index, (int)std::lround(S(96)), c.base96) ||
        !LoadIconImage(path, index, (int)std::lround(S(24)), c.base24)) {
        ui::Toast(ui::ToastKind::Error, "Couldn't read an icon from that file.");
        return;
    }
    m_cand = std::move(c);
    m_adjust = IconAdjust();
    RefreshCandidatePreview();
}

void App::SetCandidatePixels(Image master, const std::string& id, const std::string& label)
{
    if (master.empty()) return;
    Candidate c;
    c.master = std::move(master);
    c.id = id;
    c.label = label;
    c.base96 = MakeSquareResized(c.master, (int)std::lround(S(96)));
    c.base24 = MakeSquareResized(c.master, (int)std::lround(S(24)));
    m_cand = std::move(c);
    m_adjust = IconAdjust();
    RefreshCandidatePreview();
}

Image App::CandidateImage(int size) const
{
    Image img;
    if (!m_cand.path.empty()) LoadIconImage(m_cand.path, m_cand.index, size, img);
    else if (!m_cand.master.empty()) img = MakeSquareResized(m_cand.master, size);
    return img;
}

void App::RefreshCandidatePreview()
{
    Image large = m_cand.base96, tiny = m_cand.base24;
    ApplyAdjust(large, m_adjust);
    ApplyAdjust(tiny, m_adjust);
    m_cand.preview = Texture(large);
    m_cand.taskbar = Texture(tiny);
}

void App::CustomizeCurrentIcon()
{
    if (m_editing < 0) return;
    const PinnedShortcut& sc = m_entries[m_editing].sc;
    std::wstring path;
    int index = 0;
    ResolveShortcutIcon(sc, path, index);
    Image probe;
    if (!path.empty() && LoadIconImage(path, index, 32, probe)) {
        SetCandidate(path, index, "Current icon");
        return;
    }
    // Store apps / shell items: no icon file, so work from the shell's rendering
    Image shell;
    if (LoadShellItemImage(sc.lnkPath, 256, shell))
        SetCandidatePixels(std::move(shell), WideToUtf8(FileStem(sc.lnkPath)) + "-current", "Current icon");
    else
        ui::Toast(ui::ToastKind::Error, "Couldn't read the current icon.");
}

void App::HandlePickedFile(const std::wstring& path)
{
    std::string name = U8(FileStem(path) + LowerExt(path));
    switch (ClassifyIconSource(path)) {
    case IconSourceKind::IconLibrary:
        if (CountIcons(path) <= 0) {
            ui::Toast(ui::ToastKind::Warning, name + " doesn't contain any icons.");
            return;
        }
        m_tab = SourceTab::File;
        m_fileLib = path;
        break;
    case IconSourceKind::IcoFile:
    case IconSourceKind::ImageFile: {
        std::string err;
        std::wstring ico = ImportIconFile(path, err);
        if (ico.empty()) { ui::Toast(ui::ToastKind::Error, name + ": " + err); return; }
        SetCandidate(ico, 0, name);
        break;
    }
    default:
        ui::Toast(ui::ToastKind::Error, "Unsupported file: " + name + ". Use .png, .jpg, .ico, .exe or .dll.");
    }
}

void App::ApplyCommandLine(int argc, wchar_t** argv)
{
    for (int i = 1; i + 1 < argc; ++i) {
        std::wstring arg = argv[i], val = argv[i + 1];
        if (arg == L"--page") {
            m_page = val == L"restore" ? Page::Restore : val == L"settings" ? Page::Settings : Page::Pinned;
            ++i;
        } else if (arg == L"--open") {
            // prefer the live pin over a leftover copy with the same name
            for (int pass = 0; pass < 2 && m_editing < 0; ++pass)
                for (int j = 0; j < (int)m_entries.size(); ++j)
                    if (m_entries[j].sc.onTaskbar == (pass == 0) &&
                        _wcsicmp(m_entries[j].sc.displayName.c_str(), val.c_str()) == 0) { OpenEditor(j); break; }
            ++i;
        } else if (arg == L"--icon" && m_editing >= 0) {
            // "file" or "file.exe,index": preview it, nothing is applied
            size_t comma = val.rfind(L',');
            bool hasIndex = comma != std::wstring::npos && comma + 1 < val.size() &&
                            val.find_first_not_of(L"0123456789", comma + 1) == std::wstring::npos;
            std::wstring file = ExpandEnv(hasIndex ? val.substr(0, comma) : val);
            if (hasIndex && ClassifyIconSource(file) == IconSourceKind::IconLibrary) {
                int index = _wtoi(val.c_str() + comma + 1);
                bool own = _wcsicmp(file.c_str(), m_entries[m_editing].sc.targetPath.c_str()) == 0;
                m_tab = own ? SourceTab::ThisApp : SourceTab::File;
                if (!own) m_fileLib = file;
                SetCandidate(file, index, U8(FileStem(file)) + " #" + std::to_string(index));
            } else {
                HandlePickedFile(file);
            }
            ++i;
        } else if (arg == L"--adjust" && m_cand) {
            // "hue=40,saturation=140,brightness=-10,contrast=110,tint=50"
            std::string spec = WideToUtf8(val);
            for (size_t pos = 0; pos < spec.size();) {
                size_t end = spec.find_first_of(", ", pos);
                if (end == std::string::npos) end = spec.size();
                std::string kv = spec.substr(pos, end - pos);
                size_t eq = kv.find('=');
                if (eq != std::string::npos) {
                    std::string k = kv.substr(0, eq);
                    float v = (float)atof(kv.c_str() + eq + 1);
                    if (k == "hue") m_adjust.hue = std::clamp(v, -180.0f, 180.0f);
                    else if (k == "saturation") m_adjust.saturation = std::clamp(v, 0.0f, 200.0f);
                    else if (k == "brightness") m_adjust.brightness = std::clamp(v, -100.0f, 100.0f);
                    else if (k == "contrast") m_adjust.contrast = std::clamp(v, 0.0f, 200.0f);
                    else if (k == "tint") m_adjust.tintAmount = std::clamp(v, 0.0f, 100.0f);
                }
                pos = end + 1;
            }
            RefreshCandidatePreview();
            ++i;
        }
    }
}

void App::OnFilesDropped(const std::vector<std::wstring>& files)
{
    if (files.empty()) return;
    if (m_page != Page::Pinned || m_editing < 0) {
        ui::Toast(ui::ToastKind::Info, "Open an app first, then drop the icon on it.");
        return;
    }
    HandlePickedFile(files.front());
}

void App::BrowseForIcon()
{
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
        return;
    const COMDLG_FILTERSPEC filters[] = {
        { L"All supported", L"*.ico;*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tga;*.exe;*.dll;*.icl;*.cpl;*.mun" },
        { L"Images", L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tga" },
        { L"Icons", L"*.ico" },
        { L"Programs and libraries", L"*.exe;*.dll;*.icl;*.cpl;*.mun" },
    };
    dlg->SetFileTypes((UINT)std::size(filters), filters);
    dlg->SetTitle(L"Choose an icon");
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_NOCHANGEDIR);
    if (FAILED(dlg->Show(m_hwnd))) return;
    ComPtr<IShellItem> item;
    PWSTR path = nullptr;
    if (SUCCEEDED(dlg->GetResult(&item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        HandlePickedFile(path);
        CoTaskMemFree(path);
    }
}

void App::ApplyCandidate()
{
    if (m_editing < 0 || !m_cand) return;
    Entry& e = m_entries[m_editing];
    IconBackupEntry original{ e.sc.iconPath, e.sc.iconIndex };

    // An untouched icon inside a file is referenced directly. Anything adjusted or
    // downloaded becomes a multi-size .ico of its own in the Iconger icons folder.
    std::wstring iconPath = m_cand.path;
    int iconIndex = m_cand.index;
    if (m_cand.path.empty() || !m_adjust.IsIdentity()) {
        std::vector<Image> sizes;
        for (int sz : { 256, 128, 64, 48, 32, 24, 16 }) {
            Image img = CandidateImage(sz);
            if (img.empty()) break;
            ApplyAdjust(img, m_adjust);
            sizes.push_back(std::move(img));
        }
        std::string key = m_cand.id + "|" + WideToUtf8(m_cand.path) + "|" + std::to_string(m_cand.index) + "|" + m_adjust.Key();
        uint64_t h = 1469598103934665603ull;
        for (unsigned char c : key) { h ^= c; h *= 1099511628211ull; }
        std::wstring name = Utf8ToWide(m_cand.id);
        for (auto& c : name)
            if (wcschr(L"<>:\"/\\|?*#", c) || c < 32) c = L'_';
        wchar_t hash[20];
        swprintf_s(hash, L"%08llx", (unsigned long long)(h & 0xffffffffull));
        iconPath = IconsDir() + L"\\" + name.substr(0, 40) + L"-" + hash + L".ico";
        iconIndex = 0;
        if (sizes.size() != 7 || !WriteIco(iconPath, sizes)) {
            ui::Toast(ui::ToastKind::Error, "Couldn't create the icon file.");
            return;
        }
    }

    if (!SetShortcutIcon(e.sc.lnkPath, iconPath, iconIndex)) {
        ui::Toast(ui::ToastKind::Error, "Couldn't save " + U8(e.sc.displayName) + ". Is the shortcut read-only?");
        return;
    }
    m_backup.RecordIfMissing(e.sc.lnkPath, original);
    if (!m_backup.Save())
        ui::Toast(ui::ToastKind::Warning, "Icon applied, but the backup file couldn't be written.");

    ReloadEntry(e);
    m_cand = Candidate();
    m_adjust = IconAdjust();
    // SetShortcutIcon + this notification are enough for the taskbar to redraw;
    // restarting Explorer is only a manual fallback (Settings).
    SignalIconChange();
    ui::Toast(ui::ToastKind::Success, "New icon applied to " + U8(e.sc.displayName) + ".");
}

void App::RestoreOriginal(const std::wstring& lnkPath, bool quiet)
{
    const IconBackupEntry* b = m_backup.Get(lnkPath);
    // No backup = changed before backups existed; clearing the override is the best guess.
    std::wstring path = b ? b->iconPath : L"";
    int index = b ? b->iconIndex : 0;
    std::string name = U8(FileStem(lnkPath));
    for (const auto& e : m_entries) // "Firefox", not "Firefox (2)"
        if (_wcsicmp(e.sc.lnkPath.c_str(), lnkPath.c_str()) == 0) name = U8(e.sc.displayName);

    if (FileExists(lnkPath) && !SetShortcutIcon(lnkPath, path, index)) {
        ui::Toast(ui::ToastKind::Error, "Couldn't restore " + name + ".");
        return;
    }
    m_backup.Remove(lnkPath);
    m_backup.Save();
    for (auto& e : m_entries)
        if (_wcsicmp(e.sc.lnkPath.c_str(), lnkPath.c_str()) == 0) ReloadEntry(e);
    SignalIconChange();
    if (!quiet) ui::Toast(ui::ToastKind::Success, "Restored the original icon of " + name + ".");
}

void App::RestoreAll()
{
    std::wstring folder = GetPinnedShortcutsFolder();
    std::vector<std::wstring> keys;
    for (const auto& kv : m_backup.Entries()) keys.push_back(folder + L"\\" + kv.first);
    for (const auto& lnk : keys) RestoreOriginal(lnk, true);
    ui::Toast(ui::ToastKind::Success, "Restored " + std::to_string(keys.size()) + " icon(s).");
}

void App::RecycleLeftovers()
{
    // Recycle Bin, not delete: the user can always get them back.
    std::wstring from;
    int n = 0;
    for (const auto& e : m_entries)
        if (!e.sc.onTaskbar) { from += e.sc.lnkPath; from.push_back(L'\0'); ++n; }
    if (!n) return;
    from.push_back(L'\0');
    SHFILEOPSTRUCTW op = {};
    op.hwnd = m_hwnd;
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
    if (SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted)
        ui::Toast(ui::ToastKind::Success, "Moved " + std::to_string(n) + " leftover shortcut(s) to the Recycle Bin.");
    else
        ui::Toast(ui::ToastKind::Error, "Couldn't move the leftover shortcuts.");
    Reload();
}

void App::RequestRestart()
{
    if (m_restarting) return;
    if (m_settings.confirmRestart) m_openConfirm = true;
    else StartRestart();
}

void App::StartRestart()
{
    if (m_restarting) return;
    if (m_restartThread.joinable()) m_restartThread.join();
    m_restarting = true;
    m_restartDone = false;
    bool clear = m_settings.clearIconCache;
    m_restartThread = std::thread([this, clear] {
        m_restartResult = RestartExplorer(clear);
        m_restartDone = true;
    });
}

void App::PollRestart()
{
    if (!m_restartDone) return;
    m_restartThread.join();
    m_restarting = false;
    m_restartDone = false;
    if (m_restartResult.ok) {
        ui::Toast(ui::ToastKind::Success, m_restartResult.forced
            ? "Explorer restarted (it had to be force-closed)."
            : "Explorer restarted.");
    } else {
        ui::Toast(ui::ToastKind::Error, "Explorer didn't come back. Press Ctrl+Shift+Esc, then File > Run new task > explorer.exe");
    }
}

// ============================================================================
// Icon grid
// ============================================================================

void App::IconGrid::Open(const std::wstring& file)
{
    Clear();
    path = file;
    count = std::min(CountIcons(file), 2000);
    textures.resize(std::max(count, 0));
}

bool App::IconGrid::Pump(int budget, int size)
{
    for (; loaded < count && budget > 0; ++loaded, --budget) {
        Image img;
        if (LoadIconImage(path, loaded, size, img)) textures[loaded] = Texture(img);
    }
    return loaded < count;
}

// ============================================================================
// Frame
// ============================================================================

void App::Frame()
{
    PollRestart();
    m_jobs.RunCompleted();
    QueueThumbnails();
    HandleShortcuts();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollWithMouse);

    float sidebarW = S(212);
    DrawSidebar(sidebarW);

    // Content panel: rounded top-left corner + hairline border.
    ImVec2 wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();
    ImVec2 c0(wp.x + sidebarW, wp.y + S(8));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = S(14);
    dl->AddRectFilled(c0, ImVec2(wp.x + ws.x + r, wp.y + ws.y + r), panel, r, ImDrawFlags_RoundCornersTopLeft);
    dl->AddRect(c0, ImVec2(wp.x + ws.x + r, wp.y + ws.y + r), border, r, S(1), ImDrawFlags_RoundCornersTopLeft);

    ImGui::SetCursorScreenPos(ImVec2(c0.x + S(1), c0.y + S(1)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(28), S(24)));
    ImGui::BeginChild("##content", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    switch (m_page) {
    case Page::Pinned:   m_editing >= 0 ? DrawEditor() : DrawPinnedPage(); break;
    case Page::Restore:  DrawRestorePage(); break;
    case Page::Settings: DrawSettingsPage(); break;
    }
    ImGui::EndChild();

    DrawModals();
    ImGui::End();
    ui::RenderToasts();
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
    ImGui::SetCursorPos(ImVec2(S(18), S(16)));
    ui::Logo(S(30));
    ImGui::SameLine(0, S(10));
    ImGui::SetCursorPosY(S(16) + (S(30) - ImGui::GetFontSize() * fontH2 / fontBody) * 0.5f);
    ImGui::PushFont(fonts.bold, fontH2);
    ImGui::TextUnformatted("Iconger");
    ImGui::PopFont();
    if (ICONGER_VERSION[0] == '0') { // major version 0 = beta (see CMakeLists.txt)
        ImGui::SameLine(0, S(8));
        ImGui::SetCursorPosY(S(16) + (S(30) - S(20)) * 0.5f);
        ui::Badge("BETA", primary, primarySoft);
    }

    ImGui::SetCursorPosY(S(76));
    struct Item { Page page; const char* icon; const char* label; int badge; };
    const Item items[] = {
        { Page::Pinned,   ICON_PIN,      "Pinned apps", 0 },
        { Page::Restore,  ICON_HISTORY,  "Restore",     (int)m_backup.Entries().size() },
        { Page::Settings, ICON_SETTINGS, "Settings",    0 },
    };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (const Item& it : items) {
        ImGui::SetCursorPosX(S(12));
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 size(width - S(24), S(40));
        ImGui::PushID(it.label);
        if (ImGui::InvisibleButton("##nav", size)) {
            if (it.page == Page::Pinned && m_page == Page::Pinned) CloseEditor(); // acts as "back to list"
            m_page = it.page;
        }
        bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        bool active = m_page == it.page;
        if (active || hovered)
            dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), active ? primarySoft : card, S(8));
        if (active)
            dl->AddRectFilled(ImVec2(p.x - S(12), p.y + S(8)), ImVec2(p.x - S(9), p.y + size.y - S(8)), primary, S(2));
        ImU32 col = active ? primary : hovered ? text : textSecondary;
        float ty = p.y + (size.y - ImGui::GetFontSize()) * 0.5f;
        dl->AddText(ImVec2(p.x + S(14), ty), col, it.icon);
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
// Pinned apps page
// ============================================================================

void App::DrawPinnedPage()
{
    // header
    float x0 = ImGui::GetCursorPosX(); // SameLine(x) counts from the window edge, not the padded content
    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::BeginGroup();
    ImGui::PushFont(fonts.bold, fontH1);
    ImGui::TextUnformatted("Pinned apps");
    ImGui::PopFont();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - S(8));
    ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
    size_t live = std::count_if(m_entries.begin(), m_entries.end(), [](const Entry& e) { return e.sc.onTaskbar; });
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

    ImGui::Dummy(ImVec2(0, S(6)));

    if (m_entries.empty()) {
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
    for (int pass = 0; pass < 2; ++pass) {
        const bool liveSection = pass == 0;
        int inSection = 0;
        for (int i = 0; i < (int)m_entries.size(); ++i) {
            if (m_entries[i].sc.onTaskbar != liveSection) continue;
            if (!ContainsNoCase(U8(m_entries[i].sc.displayName), m_search)) continue;
            if (!liveSection && inSection == 0) {
                // section header for shortcuts Windows orphaned on re-pin
                ImGui::Dummy(ImVec2(0, S(12)));
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
            DrawAppCard(i, cardW);
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
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 q(p.x + size.x, p.y + size.y);
    dl->AddRectFilled(p, q, hovered ? cardHover : card, S(12));
    dl->AddRect(p, q, hovered ? borderStrong : border, S(12), S(1));

    float icon = S(40);
    ImVec2 ip(p.x + S(16), p.y + (size.y - icon) * 0.5f);
    ImU32 tint = e.sc.onTaskbar ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 110);
    if (e.icon) dl->AddImage(e.icon.Id(), ip, ImVec2(ip.x + icon, ip.y + icon), ImVec2(0, 0), ImVec2(1, 1), tint);
    else dl->AddRectFilled(ip, ImVec2(ip.x + icon, ip.y + icon), accentBg, S(8));

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
    } else if (IsCustomized(e)) {
        dl->AddText(ImVec2(tx, y2), success, ICON_CHECK " Custom icon");
    } else {
        std::string sub = e.sc.targetPath.empty() ? "Windows app" : U8(FileStem(e.sc.targetPath) + LowerExt(e.sc.targetPath));
        ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
        ImGui::RenderTextEllipsis(dl, ImVec2(tx, y2), ImVec2(tx + textW, y2 + S(20)), tx + textW, sub.c_str(), nullptr, nullptr);
        ImGui::PopStyleColor();
    }
    ImGui::PopFont();

    ImVec2 cs = ImGui::CalcTextSize(ICON_CHEVRON_RIGHT);
    dl->AddText(ImVec2(q.x - S(16) - cs.x, p.y + (size.y - cs.y) * 0.5f), hovered ? text : textMuted, ICON_CHEVRON_RIGHT);

    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (clicked) OpenEditor(index);
}

// ============================================================================
// Editor
// ============================================================================

void App::DrawEditor()
{
    Entry& e = m_entries[m_editing];
    if (ui::Button("Pinned apps", ICON_ARROW_LEFT, ui::ButtonKind::Ghost)) { CloseEditor(); return; }
    ui::Tooltip("Back (Esc)");

    ImGui::PushFont(fonts.bold, fontH1);
    ImGui::TextUnformatted(U8(e.sc.displayName).c_str());
    ImGui::PopFont();
    ImGui::SameLine(0, S(12));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(8));
    if (IsCustomized(e)) ui::Badge("CUSTOM ICON", success, successSoft);
    else ui::Badge("ORIGINAL ICON", textDim, accentBg);

    ImGui::Dummy(ImVec2(0, S(4)));
    if (!e.sc.onTaskbar) {
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
    DrawSourceCard();
    ImGui::EndChild();
}

void App::DrawPreviewCard()
{
    Entry& e = m_entries[m_editing];
    ui::BeginCard("##preview");
    ui::Heading("Preview");
    ImGui::Dummy(ImVec2(0, S(2)));

    float tile = S(96);
    float avail = ImGui::GetContentRegionAvail().x;
    float arrowW = S(40);
    float startX = ImGui::GetCursorPosX() + (avail - tile * 2 - arrowW) * 0.5f;
    ImGui::SetCursorPosX(startX);
    IconFrame(m_editingPreview, tile, "Current", false);
    ImGui::SameLine(0, 0);
    ImVec2 ap = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(arrowW, tile));
    ImVec2 as = ImGui::CalcTextSize(ICON_ARROW_RIGHT);
    ImGui::GetWindowDrawList()->AddText(ImVec2(ap.x + (arrowW - as.x) * 0.5f, ap.y + (tile - as.y) * 0.5f),
                                        m_cand ? primary : textMuted, ICON_ARROW_RIGHT);
    ImGui::SameLine(0, 0);
    IconFrame(m_cand.preview, tile, m_cand ? "New" : "Pick an icon", (bool)m_cand);

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
    bool canReset = customized || (!e.sc.iconPath.empty() && !usesOwnIcon);
    if (m_cand) {
        if (ui::Button("Discard selection", ICON_X, ui::ButtonKind::Ghost, ImVec2(-1, 0))) {
            m_cand = Candidate();
            m_adjust = IconAdjust();
        }
    } else if (ui::Button(customized ? "Restore original icon" : "Use the app's own icon", ICON_UNDO,
                          ui::ButtonKind::Secondary, ImVec2(-1, 0), canReset)) {
        RestoreOriginal(e.sc.lnkPath);
    }
    if (!canReset && !m_cand) ui::Tooltip("This shortcut already uses the app's own icon.");
    ui::EndCard();

    // details
    ImGui::Spacing();
    ui::BeginCard("##details");
    ui::Heading("Details");
    float w = ImGui::GetContentRegionAvail().x;
    auto row = [&](const char* label, const std::string& value) {
        SectionLabel(label);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - S(8));
        ui::TextEllipsis(value.empty() ? "-" : value.c_str(), w, textDim);
    };
    row("SHORTCUT", U8(FileStem(e.sc.lnkPath)) + ".lnk");
    row("LAUNCHES", e.sc.targetPath.empty() ? "Windows / Store app" : U8(e.sc.targetPath));
    std::wstring iconPath;
    int iconIndex = 0;
    ResolveShortcutIcon(e.sc, iconPath, iconIndex);
    row("ICON FROM", iconPath.empty() ? "App default" : U8(iconPath) + (iconIndex ? "  #" + std::to_string(iconIndex) : ""));
    ImGui::Dummy(ImVec2(0, S(2)));
    if (ui::Button("Show shortcut in Explorer", ICON_EXTERNAL, ui::ButtonKind::Secondary, ImVec2(-1, 0)))
        ShowInExplorer(e.sc.lnkPath);
    ui::EndCard();
}

void App::DrawAdjustPanel()
{
    ImGui::PushID("adjust");
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

    changed |= ui::Slider("Hue", &m_adjust.hue, -180, 180, 0, "%+.0f°", ui::SliderTrack::Hue);
    changed |= ui::Slider("Saturation", &m_adjust.saturation, 0, 200, 100, "%.0f%%");
    changed |= ui::Slider("Brightness", &m_adjust.brightness, -100, 100, 0, "%+.0f");
    changed |= ui::Slider("Contrast", &m_adjust.contrast, 0, 200, 100, "%.0f%%");
    changed |= ui::Slider("Tint", &m_adjust.tintAmount, 0, 100, 0, "%.0f%%");

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
    for (int i = 0; i < tabCount; ++i) {
        ImGui::SetCursorScreenPos(ImVec2(segP.x + S(4) + tabW * i, segP.y + S(4)));
        bool active = m_tab == tabs[i].tab;
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(7));
        if (ui::Button(tabs[i].label, tabs[i].icon, active ? ui::ButtonKind::Secondary : ui::ButtonKind::Ghost,
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

void App::EnsureIndex()
{
    if (!m_settings.onlineLibraries || m_index || m_indexLoading) return;
    m_indexLoading = true;
    m_indexError.clear();
    m_jobs.Submit([this]() -> JobPool::Done {
        auto index = std::make_shared<IconIndex>();
        std::string err;
        bool ok = index->Load(err);
        return [this, index, ok, err] {
            m_indexLoading = false;
            if (ok) m_index = index;
            else m_indexError = err;
            m_libQueryRan.clear();
            RunLibrarySearch();
        };
    });
}

void App::RunLibrarySearch()
{
    if (m_editing < 0 || !m_index || !m_settings.onlineLibraries) return;
    std::string q = m_libQuery;
    if (q == m_libQueryRan) return;
    m_libQueryRan = q;

    std::vector<std::string> queries = { q };
    // for the default query also try the exe name ("Code.exe" for Visual Studio Code)
    const PinnedShortcut& sc = m_entries[m_editing].sc;
    if (q == U8(sc.displayName) && !sc.targetPath.empty()) queries.push_back(U8(FileStem(sc.targetPath)));

    ++m_onlineGen; // results still downloading for the old query are dropped
    m_thumbsInFlight = 0;
    m_online.clear();
    for (auto& hit : m_index->Search(queries, 36)) {
        OnlineTile t;
        t.icon = std::move(hit);
        m_online.push_back(std::move(t));
    }
    QueueThumbnails();
}

void App::QueueThumbnails()
{
    // debounce typing in the search box
    if (m_libEditTime >= 0 && ImGui::GetTime() - m_libEditTime > 0.35) {
        m_libEditTime = -1;
        RunLibrarySearch();
    }
    const int size = (int)std::lround(S(40));
    for (size_t i = 0; i < m_online.size() && m_thumbsInFlight < 8; ++i) {
        OnlineTile& t = m_online[i];
        if (!t.loading || t.queued) continue;
        t.queued = true;
        ++m_thumbsInFlight;
        m_jobs.Submit([this, gen = m_onlineGen, i, icon = t.icon, size]() -> JobPool::Done {
            Image img;
            std::string err;
            bool ok = FetchLibraryIcon(icon, size, img, err);
            return [this, gen, i, ok, img] {
                if (gen != m_onlineGen || i >= m_online.size()) return;
                --m_thumbsInFlight;
                OnlineTile& tile = m_online[i];
                tile.loading = false;
                tile.failed = !ok;
                if (!ok) return;
                uint64_t h = 1469598103934665603ull;
                for (uint8_t b : img.rgba) { h ^= b; h *= 1099511628211ull; }
                tile.hash = h;
                for (size_t k = 0; k < m_online.size(); ++k)
                    if (k != i && !m_online[k].loading && m_online[k].hash == h && !m_online[k].duplicate)
                        tile.duplicate = true;
                if (!tile.duplicate) tile.tex = Texture(img);
            };
        });
    }
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
        ImGui::PopID();
        if (!ImGui::IsItemVisible()) continue;
        ImVec2 q(p.x + cell, p.y + cell);
        if (selected(i)) {
            dl->AddRectFilled(p, q, primarySoft, S(8));
            dl->AddRect(p, q, primary, S(8), S(1.5f));
        } else if (hov) {
            dl->AddRectFilled(p, q, cardHover, S(8));
        }
        ImVec2 ip(p.x + (cell - ic) * 0.5f, p.y + (cell - ic) * 0.5f);
        const Texture* t = tex(i);
        if (t && *t) {
            dl->AddImage(t->Id(), ip, ImVec2(ip.x + ic, ip.y + ic));
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

        if (m_indexLoading) {
            ui::Spinner(S(18), primary);
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
            ImGui::TextUnformatted("Loading icon libraries...");
            ImGui::PopStyleColor();
        } else if (!m_index) {
            ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
            ImGui::TextWrapped("Couldn't load the icon libraries (%s). Check your internet connection.", m_indexError.c_str());
            ImGui::PopStyleColor();
            if (ui::Button("Try again", ICON_REFRESH, ui::ButtonKind::Secondary)) EnsureIndex();
        } else if (std::none_of(m_online.begin(), m_online.end(), [](const OnlineTile& t) { return t.loading || (!t.failed && !t.duplicate); })) {
            ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
            ImGui::TextWrapped("No icons match \"%s\". Try a shorter name, or a word like \"notes\" or \"music\".", m_libQuery);
            ImGui::PopStyleColor();
        } else {
            std::vector<int> shown; // failed downloads and repeats of the same picture are hidden
            for (int i = 0; i < (int)m_online.size(); ++i)
                if (!m_online[i].failed && !m_online[i].duplicate) shown.push_back(i);
            int hit = DrawTiles("online", (int)shown.size(),
                [&](int k) -> const Texture* { return &m_online[shown[k]].tex; },
                [&](int k) { return m_cand && m_cand.path.empty() && m_cand.id == m_online[shown[k]].icon.Id(); },
                [&](int k) { return m_online[shown[k]].loading; },
                [&](int k) {
                    const LibraryIcon& ic = m_online[shown[k]].icon;
                    return std::string(LibraryName(ic.lib)) + ": " + (ic.title.empty() ? ic.name : ic.title);
                });
            if (hit >= 0) {
                // the file is cached by now, so a full-size render is quick
                const LibraryIcon& ic = m_online[shown[hit]].icon;
                Image master;
                std::string err;
                if (FetchLibraryIcon(ic, 256, master, err))
                    SetCandidatePixels(std::move(master), ic.Id(), std::string(LibraryName(ic.lib)) + ": " + ic.name);
                else
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

// ============================================================================
// Restore page
// ============================================================================

void App::DrawRestorePage()
{
    float x0 = ImGui::GetCursorPosX();
    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::BeginGroup();
    ImGui::PushFont(fonts.bold, fontH1);
    ImGui::TextUnformatted("Restore");
    ImGui::PopFont();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - S(8));
    ImGui::PushStyleColor(ImGuiCol_Text, textSecondary);
    ImGui::TextUnformatted("Iconger remembers every shortcut's original icon before changing it.");
    ImGui::PopStyleColor();
    ImGui::EndGroup();

    const auto& backups = m_backup.Entries();
    if (!backups.empty()) {
        float bw = S(150);
        ImGui::SameLine(x0 + avail - bw);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(8));
        if (ui::Button("Restore all", ICON_ROTATE_CCW, ui::ButtonKind::Danger, ImVec2(bw, 0))) m_openRestoreAll = true;
    }
    ImGui::Dummy(ImVec2(0, S(6)));

    if (backups.empty()) {
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
    for (const auto& [key, b] : backups) {
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
    ImGui::EndChild();

    // mutate after iterating the map
    if (!restoreLnk.empty()) RestoreOriginal(restoreLnk);
    if (!forgetLnk.empty()) { m_backup.Remove(forgetLnk); m_backup.Save(); }
}

// ============================================================================
// Settings page
// ============================================================================

void App::DrawSettingsPage()
{
    ImGui::PushFont(fonts.bold, fontH1);
    ImGui::TextUnformatted("Settings");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, S(4)));

    ImGui::BeginChild("##settings", ImVec2(0, 0), 0, ImGuiWindowFlags_NoBackground);
    float maxW = std::min(ImGui::GetContentRegionAvail().x, S(760));

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
    if (changed) m_settings.Save();

    ImGui::Spacing();
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
        if (m_settings.onlineLibraries) EnsureIndex();
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
        m_index.reset();
        m_online.clear();
        m_libQueryRan.clear();
        ++m_onlineGen;
        ui::Toast(ui::ToastKind::Success, "Cleared " + std::to_string(n) + " downloaded file(s).");
    }
    ui::Tooltip("Icons you already applied are kept.");
    ui::EndCard();

    ImGui::Spacing();
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

    ImGui::Spacing();
    ui::BeginCard("##about", ImVec2(maxW, 0), 20);
    ui::Logo(S(40));
    ImGui::SameLine(0, S(14));
    ImGui::BeginGroup();
    ui::Heading("Iconger v" ICONGER_VERSION, "Open source (MIT). Shortcuts: F5 reload, Ctrl+F search, Ctrl+O browse, Ctrl+S apply, Esc back.");
    ImGui::EndGroup();
    if (ui::Button("GitHub", ICON_CODE, ui::ButtonKind::Outline))
        ShellExecuteA(nullptr, "open", kRepoUrl, nullptr, nullptr, SW_SHOWNORMAL);
    ui::EndCard();
    ImGui::EndChild();
}

// ============================================================================
// Modals
// ============================================================================

void App::DrawModals()
{
    auto beginModal = [](const char* id) {
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(S(420), 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(24), S(22)));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, card);
        bool open = ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        return open;
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
        ImGui::EndPopup();
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
        ImGui::EndPopup();
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
        ImGui::EndPopup();
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
        ImGui::EndPopup();
    }
}
