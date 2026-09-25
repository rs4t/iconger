// Lifecycle, data and actions (apply, restore, Explorer restart).
#include "app_internal.h"

using namespace appui;
using Microsoft::WRL::ComPtr;

// ============================================================================
// Lifecycle / data
// ============================================================================

void App::Init(HWND hwnd, float dpiScale)
{
    m_hwnd = hwnd;
    m_startedAt = ImGui::GetTime();
    m_settings.Load();
    m_backup.Load();
    CleanupAfterUpdate(ExePath());
    ApplyStyle(dpiScale);
    Reload();
    // on the very first run these wait for the welcome screen's choices
    if (m_settings.welcomed) {
        if (m_settings.startMenuShortcut) EnsureStartMenuShortcut(ExePath());
        if (m_settings.checkUpdates) StartUpdateCheck(false);
    }
}

void App::Shutdown()
{
    if (m_restartThread.joinable()) m_restartThread.join();
    m_entries.clear();
    m_editingPreview.Reset();
    m_cand = Candidate();
    m_adjust = IconAdjust();
    m_grid.Clear();
    m_library.Clear();
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
    // library thumbnails re-render on their own: LibrarySearch::Pump sees the new size
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
        LoadShellItemImage(ShellPath(sc), size, img);
    return Texture(img);
}

void App::Reload()
{
    std::wstring keepLnk = m_editing >= 0 ? EntryKey(m_entries[m_editing].sc) : L"";
    m_entries.clear();
    bool backupChanged = false;
    for (auto& sc : EnumeratePinnedShortcuts()) {
        // A pin made from one of our packaged-app shortcuts started out as the app's own
        // icon. Remember that, so Restore can give it back like for any other pin.
        // (Only while it has a custom icon: once restored, it has none.)
        if (!sc.iconPath.empty() && !m_backup.Has(sc.lnkPath) && !IcongerAppShortcut(sc).empty()) {
            m_backup.RecordIfMissing(sc.lnkPath, IconBackupEntry{ L"", 0 });
            backupChanged = true;
        }
        Entry e;
        e.sc = std::move(sc);
        e.icon = LoadEntryIcon(e.sc, S(40));
        m_entries.push_back(std::move(e));
    }
    if (backupChanged) m_backup.Save();
    m_editing = -1;
    if (!keepLnk.empty()) {
        for (int i = 0; i < (int)m_entries.size(); ++i)
            if (_wcsicmp(EntryKey(m_entries[i].sc).c_str(), keepLnk.c_str()) == 0) m_editing = i;
        if (m_editing < 0) CloseEditor(); // it was unpinned meanwhile
    }
}

void App::ReloadEntry(Entry& e)
{
    PinnedShortcut fresh;
    if (!e.sc.packaged && ReadShortcut(e.sc.lnkPath, fresh)) e.sc = std::move(fresh);
    e.icon = LoadEntryIcon(e.sc, S(40));
    if (m_editing >= 0 && &m_entries[m_editing] == &e) m_editingPreview = LoadEntryIcon(e.sc, S(96));
}

bool App::IsCustomized(const Entry& e) const { return !e.sc.packaged && m_backup.Has(e.sc.lnkPath); }

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
    m_libEditTime = -1;
    m_library.Clear();
    if (m_settings.onlineLibraries) m_library.EnsureIndex();
    SearchLibraries();
}

void App::CloseEditor()
{
    m_editing = -1;
    m_editingPreview.Reset();
    m_cand = Candidate();
    m_adjust = IconAdjust();
    m_grid.Clear();
    m_library.Clear();
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
    if (!m_cand) m_adjustAt = ImGui::GetTime();
    m_candAt = ImGui::GetTime();
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
    if (!m_cand) m_adjustAt = ImGui::GetTime();
    m_candAt = ImGui::GetTime();
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
    if (LoadShellItemImage(ShellPath(sc), 256, shell))
        SetCandidatePixels(std::move(shell), WideToUtf8(sc.displayName) + "-current", "Current icon");
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
    // Collect first, then act in dependency order (--icon needs the editor from --open,
    // --adjust needs the icon), so the options work in any order.
    std::wstring page, open, icon, adjust;
    for (int i = 1; i + 1 < argc; i += 2) {
        std::wstring arg = argv[i], val = argv[i + 1];
        if (arg == L"--page") page = val;
        else if (arg == L"--open") open = val;
        else if (arg == L"--icon") icon = val;
        else if (arg == L"--adjust") adjust = val;
        else --i; // unknown flag: its "value" may be the next flag
    }

    if (!page.empty())
        m_page = page == L"restore" ? Page::Restore : page == L"settings" ? Page::Settings : Page::Pinned;
    if (!open.empty()) {
        // prefer the live pin over a leftover copy with the same name
        for (int pass = 0; pass < 2 && m_editing < 0; ++pass)
            for (int j = 0; j < (int)m_entries.size(); ++j)
                if (m_entries[j].sc.onTaskbar == (pass == 0) &&
                    _wcsicmp(m_entries[j].sc.displayName.c_str(), open.c_str()) == 0) { OpenEditor(j); break; }
    }
    if (!icon.empty() && m_editing >= 0) {
        if (icon == L"current") {
            CustomizeCurrentIcon(); // same as the "Customize current icon" button
        } else {
            // "file" or "file.exe,index": preview it, nothing is applied
            size_t comma = icon.rfind(L',');
            bool hasIndex = comma != std::wstring::npos && comma + 1 < icon.size() &&
                            icon.find_first_not_of(L"0123456789", comma + 1) == std::wstring::npos;
            std::wstring file = ExpandEnv(hasIndex ? icon.substr(0, comma) : icon);
            if (hasIndex && ClassifyIconSource(file) == IconSourceKind::IconLibrary) {
                int index = _wtoi(icon.c_str() + comma + 1);
                bool own = _wcsicmp(file.c_str(), m_entries[m_editing].sc.targetPath.c_str()) == 0;
                m_tab = own ? SourceTab::ThisApp : SourceTab::File;
                if (!own) m_fileLib = file;
                SetCandidate(file, index, U8(FileStem(file)) + " #" + std::to_string(index));
            } else {
                HandlePickedFile(file);
            }
        }
    }
    if (!adjust.empty() && m_cand) {
        // "hue=40,saturation=140,brightness=-10,contrast=110,tint=50"
        std::string spec = WideToUtf8(adjust);
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

    // Every icon becomes a multi-size .ico of its own in the Iconger icons folder, so it
    // keeps working when the source is deleted or an update moves or renumbers the
    // icons of an .exe/.dll. Files that are already there (imports) are used as they are.
    std::wstring iconPath = m_cand.path;
    int iconIndex = m_cand.index;
    std::wstring iconsDir = IconsDir() + L"\\";
    bool stable = !m_cand.path.empty() && m_adjust.IsIdentity() &&
                  _wcsnicmp(m_cand.path.c_str(), iconsDir.c_str(), iconsDir.size()) == 0;
    if (!stable) {
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

    if (e.sc.packaged) {
        ApplyToPackagedApp(iconPath, iconIndex);
        return;
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
    m_appliedAt = ImGui::GetTime();
    m_appliedKey = EntryKey(e.sc);
    // SetShortcutIcon + this notification are enough for the taskbar to redraw;
    // restarting Explorer is only a manual fallback (Settings).
    SignalIconChange();
    ui::Toast(ui::ToastKind::Success, "New icon applied to " + U8(e.sc.displayName) + ".");
}

void App::ApplyToPackagedApp(const std::wstring& iconPath, int iconIndex)
{
    // A packaged app's icon lives inside its signed package and can't be changed.
    // Instead: a shortcut that launches the app (same AppUserModelID, so its window
    // groups with the pin) with our icon, which the user pins in place of the original.
    // Windows 11 has no API to pin for them, hence the guide.
    const PinnedShortcut& sc = m_entries[m_editing].sc;
    std::wstring lnk = AppShortcutsFolder() + L"\\" + SafeFileName(sc.displayName) + L".lnk";
    if (!CreateAppShortcut(lnk, sc.aumid, iconPath, iconIndex)) {
        ui::Toast(ui::ToastKind::Error, "Couldn't create the shortcut for " + U8(sc.displayName) + ".");
        return;
    }
    m_cand = Candidate();
    m_adjust = IconAdjust();
    m_pinGuideLnk = lnk;
    m_pinGuideName = U8(sc.displayName);
    m_openPinGuide = true;
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
    // A packaged app's pin: the Start menu shortcut it came from goes back too.
    for (const auto& e : m_entries) {
        if (_wcsicmp(e.sc.lnkPath.c_str(), lnkPath.c_str()) != 0) continue;
        std::wstring ours = IcongerAppShortcut(e.sc);
        if (!ours.empty()) SetShortcutIcon(ours, L"", 0);
    }
    m_backup.Remove(lnkPath);
    m_backup.Save();
    for (auto& e : m_entries) {
        if (_wcsicmp(e.sc.lnkPath.c_str(), lnkPath.c_str()) != 0) continue;
        ReloadEntry(e);
        if (!quiet) { m_appliedAt = ImGui::GetTime(); m_appliedKey = EntryKey(e.sc); }
    }
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
