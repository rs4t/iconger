// Export / import of the whole setup (Settings > Your setup).
#include "app_internal.h"
#include "setup_file.h"

using namespace appui;
using Microsoft::WRL::ComPtr;

namespace {

const COMDLG_FILTERSPEC kSetupFilter[] = { { L"Iconger setup", L"*.iconger" } };

std::wstring PickSetupFile(HWND owner, bool save)
{
    ComPtr<IFileDialog> dlg;
    if (FAILED(CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg))))
        return {};
    dlg->SetFileTypes((UINT)std::size(kSetupFilter), kSetupFilter);
    dlg->SetDefaultExtension(L"iconger");
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR | (save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST));
    if (save) {
        dlg->SetTitle(L"Export your Iconger setup");
        dlg->SetFileName(L"Iconger setup.iconger");
    } else {
        dlg->SetTitle(L"Import an Iconger setup");
    }
    if (FAILED(dlg->Show(owner))) return {};
    ComPtr<IShellItem> item;
    PWSTR path = nullptr;
    std::wstring out;
    if (SUCCEEDED(dlg->GetResult(&item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) out = path;
    CoTaskMemFree(path);
    return out;
}

std::wstring FileNameOf(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring LowerW(std::wstring s)
{
    if (!s.empty()) CharLowerBuffW(s.data(), (DWORD)s.size());
    return s;
}

bool SameText(const std::wstring& a, const std::wstring& b)
{
    return !a.empty() && _wcsicmp(a.c_str(), b.c_str()) == 0;
}

// An imported icon becomes a file in the icons folder like any other (same bytes, same file).
std::wstring SaveImportedIcon(const std::wstring& name, const std::vector<uint8_t>& ico)
{
    uint64_t h = 1469598103934665603ull;
    for (uint8_t b : ico) { h ^= b; h *= 1099511628211ull; }
    wchar_t hash[20];
    swprintf_s(hash, L"%08llx", (unsigned long long)(h & 0xffffffffull));
    std::wstring file = IconsDir() + L"\\" + SafeFileName(name).substr(0, 40) + L"-" + hash + L".ico";
    if (FileExists(file)) return file;
    HANDLE f = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    DWORD written = 0;
    bool ok = WriteFile(f, ico.data(), (DWORD)ico.size(), &written, nullptr) && written == ico.size();
    CloseHandle(f);
    if (!ok) { DeleteFileW(file.c_str()); return {}; }
    return file;
}

} // namespace

void App::ExportSetup()
{
    std::vector<SetupItem> items;
    for (const Entry& e : m_entries) {
        if (e.sc.running || !e.sc.onTaskbar || !IsCustomized(e)) continue;
        std::wstring path;
        int index = 0;
        ResolveShortcutIcon(e.sc, path, index);
        SetupItem it;
        if (!IcoFileBytes(path, index, it.ico)) continue;
        it.name = e.sc.displayName;
        it.shortcut = FileNameOf(e.sc.lnkPath);
        it.program = LowerW(FileNameOf(e.sc.targetPath));
        it.aumid = e.sc.aumid;
        items.push_back(std::move(it));
    }
    for (const auto& [exe, ico] : m_winRules.All()) { // EXPERIMENTAL: apps that aren't pinned
        SetupItem it;
        if (!IcoFileBytes(ico, 0, it.ico)) continue;
        it.unpinned = true;
        it.name = ExeDisplayName(exe);
        it.program = LowerW(FileNameOf(exe));
        it.programPath = exe;
        items.push_back(std::move(it));
    }
    if (items.empty()) {
        ui::Toast(ui::ToastKind::Info, "Nothing to export yet: no app has a custom icon.");
        return;
    }

    std::wstring file = PickSetupFile(m_hwnd, true);
    if (file.empty()) return;
    std::string text = SerializeSetup(items, ICONGER_VERSION);
    HANDLE f = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD written = 0;
    bool ok = f != INVALID_HANDLE_VALUE && WriteFile(f, text.data(), (DWORD)text.size(), &written, nullptr) &&
              written == text.size();
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    if (!ok) {
        ui::Toast(ui::ToastKind::Error, "Couldn't save the setup file.");
        return;
    }
    ui::Toast(ui::ToastKind::Success, "Exported " + std::to_string(items.size()) + " icon" + (items.size() == 1 ? "" : "s") +
                                      " to " + U8(FileNameOf(file)) + ".");
}

void App::ImportSetup()
{
    std::wstring file = PickSetupFile(m_hwnd, false);
    if (file.empty()) return;
    std::vector<uint8_t> bytes;
    std::vector<SetupItem> items;
    std::string error;
    if (!ReadWholeFile(file, bytes)) {
        ui::Toast(ui::ToastKind::Error, "Couldn't read that file.");
        return;
    }
    if (!ParseSetup(std::string(bytes.begin(), bytes.end()), items, error)) {
        ui::Toast(ui::ToastKind::Error, error);
        return;
    }
    Reload(); // match against what's pinned and running right now

    m_importApplied = 0;
    m_importSkipped.clear();
    m_importNeedsFeature = false;
    bool rulesChanged = false;
    for (const SetupItem& it : items) {
        std::string name = U8(it.name.empty() ? it.program : it.name);
        if (it.unpinned) {
            // the program where it was, or wherever the same program is running now
            std::wstring exe = FileExists(it.programPath) ? it.programPath : L"";
            for (const Entry& e : m_entries)
                if (exe.empty() && e.sc.running && SameText(FileNameOf(e.sc.targetPath), it.program)) exe = e.sc.targetPath;
            if (exe.empty()) { m_importSkipped.push_back(name + ": not installed here (or not running)"); continue; }
            std::wstring ico = SaveImportedIcon(it.name, it.ico);
            if (ico.empty()) { m_importSkipped.push_back(name + ": couldn't save the icon"); continue; }
            m_winRules.Set(exe, ico);
            rulesChanged = true;
            m_importNeedsFeature |= !m_settings.unpinnedIcons;
            ++m_importApplied;
            continue;
        }

        // a pin: the same shortcut file, else a pin of the same program, else the same name
        Entry* match = nullptr;
        auto find = [&](auto pred) {
            for (Entry& e : m_entries)
                if (!match && !e.sc.running && e.sc.onTaskbar && pred(e)) match = &e;
        };
        find([&](const Entry& e) { return SameText(FileNameOf(e.sc.lnkPath), it.shortcut); });
        find([&](const Entry& e) { return SameText(FileNameOf(e.sc.targetPath), it.program); });
        find([&](const Entry& e) { return SameText(e.sc.displayName, it.name); });
        if (!match) { m_importSkipped.push_back(name + ": not pinned here"); continue; }
        if (match->sc.packaged) {
            m_importSkipped.push_back(name + ": Store app, open it in Iconger and apply the icon to pin it");
            continue;
        }
        std::wstring ico = SaveImportedIcon(it.name, it.ico);
        if (ico.empty() || !SetPinnedIcon(*match, ico, 0)) {
            m_importSkipped.push_back(name + ": couldn't change its shortcut");
            continue;
        }
        ++m_importApplied;
    }
    if (rulesChanged) {
        m_winRules.Save();
        m_keeper.ApplyAll();
        for (auto& e : m_entries)
            if (e.sc.running) ReloadEntry(e);
    }
    SignalIconChange();
    m_appliedAt = ImGui::GetTime();
    m_openImportResult = true;
}
