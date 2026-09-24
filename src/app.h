#pragma once
#include "explorer_refresh.h"
#include "gfx.h"
#include "icon_backup.h"
#include "settings.h"
#include "shell_link.h"
#include <windows.h>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

class App {
public:
    void Init(HWND hwnd, float dpiScale);
    void Shutdown();

    void SetDpiScale(float scale);
    void Frame();

    /// Command line: --page pinned|restore|settings, --open "<app name>".
    void ApplyCommandLine(int argc, wchar_t** argv);

    /// Files dropped on the window (WM_DROPFILES).
    void OnFilesDropped(const std::vector<std::wstring>& files);

    /// True while something animates or loads, so the main loop must not idle.
    bool IsBusy() const;

private:
    struct Entry {
        PinnedShortcut sc;
        Texture icon;
    };

    struct Candidate {
        std::wstring path;   // .ico under IconsDir(), or an .exe/.dll
        int index = 0;
        std::string label;
        Texture preview;
        Texture taskbar;       // taskbar-size preview
        explicit operator bool() const { return !path.empty(); }
    };

    /// Icons of one .exe/.dll, extracted a few per frame so big libraries don't freeze the UI.
    struct IconGrid {
        std::wstring path;
        int count = 0;
        int loaded = 0;
        std::vector<Texture> textures;
        void Open(const std::wstring& file);
        void Clear() { *this = IconGrid(); }
        bool Pump(int budget, int size);
    };

    enum class Page { Pinned, Restore, Settings };
    enum class SourceTab { ThisApp, Windows, File };

    // data
    void Reload();
    void ReloadEntry(Entry& e);
    Texture LoadEntryIcon(const PinnedShortcut& sc, float px) const;
    bool IsCustomized(const Entry& e) const;

    // actions
    void OpenEditor(int index);
    void CloseEditor();
    void SetCandidate(const std::wstring& path, int index, const std::string& label);
    void HandlePickedFile(const std::wstring& path);
    void BrowseForIcon();
    void ApplyCandidate();
    void RestoreOriginal(const std::wstring& lnkPath, bool quiet = false);
    void RestoreAll();
    void RecycleLeftovers();
    void RequestRestart();
    void StartRestart();
    void PollRestart();

    // views
    void DrawSidebar(float width);
    void DrawPinnedPage();
    void DrawAppCard(int index, float width);
    void DrawEditor();
    void DrawPreviewCard();
    void DrawSourceCard();
    void DrawIconGrid();
    void DrawTaskbarPreview(float width);
    void DrawRestorePage();
    void DrawSettingsPage();
    void DrawModals();
    void HandleShortcuts();

    HWND m_hwnd = nullptr;
    Settings m_settings;
    IconBackup m_backup;
    std::vector<Entry> m_entries;
    Page m_page = Page::Pinned;
    char m_search[128] = {};
    bool m_focusSearch = false;

    int m_editing = -1;
    Texture m_editingPreview;
    Candidate m_cand;
    SourceTab m_tab = SourceTab::ThisApp;
    int m_windowsLib = 0;
    std::wstring m_fileLib;
    IconGrid m_grid;

    bool m_openConfirm = false;
    bool m_openRestoreAll = false;
    bool m_openCleanup = false;

    std::thread m_restartThread;
    std::atomic<bool> m_restarting{ false };
    std::atomic<bool> m_restartDone{ false };
    ExplorerRestartResult m_restartResult;
};
