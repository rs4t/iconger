#pragma once
#include "explorer_refresh.h"
#include "gfx.h"
#include "icon_adjust.h"
#include "icon_backup.h"
#include "job_pool.h"
#include "library_search.h"
#include "settings.h"
#include "shell_link.h"
#include "updater.h"
#include <windows.h>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <functional>
#include <vector>

class App {
public:
    void Init(HWND hwnd, float dpiScale);
    void Shutdown();

    void SetDpiScale(float scale);
    void Frame();

    /// Command line: --page pinned|restore|settings, --open "<app name>", --icon, --adjust.
    /// Options may come in any order.
    void ApplyCommandLine(int argc, wchar_t** argv);

    /// Files dropped on the window (WM_DROPFILES).
    void OnFilesDropped(const std::vector<std::wstring>& files);

    /// True while something animates or loads, so the main loop must not idle.
    bool IsBusy() const;

    /// An update was installed: main should close the window and start the new exe.
    bool WantsRelaunch() const { return m_relaunch; }

private:
    struct Entry {
        PinnedShortcut sc;
        Texture icon;
    };

    /// The icon picked but not applied yet. Either an icon inside a file
    /// (path + index: .ico / .exe / .dll) or plain pixels (online libraries).
    struct Candidate {
        std::wstring path;
        int index = 0;
        Image master;          // 256 px source when path is empty
        std::string id;        // stable name for generated files, and to highlight the grid tile
        std::string label;
        Image base96, base24;  // unadjusted preview sources
        Texture preview;
        Texture taskbar;       // taskbar-size preview
        explicit operator bool() const { return !path.empty() || !master.empty(); }
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
    enum class SourceTab { ThisApp, File };

    // data
    void Reload();
    void ReloadEntry(Entry& e);
    Texture LoadEntryIcon(const PinnedShortcut& sc, float px) const;
    bool IsCustomized(const Entry& e) const;

    // actions
    void OpenEditor(int index);
    void CloseEditor();
    void SetCandidate(const std::wstring& path, int index, const std::string& label);
    void SetCandidatePixels(Image master, const std::string& id, const std::string& label);
    Image CandidateImage(int size) const;
    void RefreshCandidatePreview();
    void CustomizeCurrentIcon();
    void HandlePickedFile(const std::wstring& path);
    void BrowseForIcon();
    void ApplyCandidate();
    void ApplyToPackagedApp(const std::wstring& iconPath, int iconIndex);
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
    void DrawThisAppTab(float height);
    void DrawAdjustPanel();
    /// Flowing grid of icon tiles; returns the clicked index or -1.
    int DrawTiles(const char* id, int count, const std::function<const Texture*(int)>& tex,
                  const std::function<bool(int)>& selected, const std::function<bool(int)>& loading,
                  const std::function<std::string(int)>& tooltip);

    // online icon libraries
    void SearchLibraries();
    void DrawTaskbarPreview(float width);
    void DrawRestorePage();
    void DrawSettingsPage();
    void DrawModals();
    void DrawWelcome();
    void HandleShortcuts();

    // updates
    void StartUpdateCheck(bool manual);
    void InstallUpdate();
    void DrawUpdateStatus(float width);

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
    IconAdjust m_adjust;
    SourceTab m_tab = SourceTab::ThisApp;
    std::wstring m_fileLib;
    IconGrid m_grid;

    JobPool m_jobs{ 6 };               // declared before m_library, which holds a reference to it
    LibrarySearch m_library{ m_jobs };
    char m_libQuery[128] = {};
    double m_libEditTime = -1;         // last keystroke in the search box (debounce)

    bool m_openConfirm = false;
    bool m_openRestoreAll = false;
    bool m_openCleanup = false;
    bool m_openPinGuide = false;
    std::wstring m_pinGuideLnk;   // shortcut made for a packaged app, waiting to be pinned
    std::string m_pinGuideName;

    enum class UpdateState { Idle, Checking, UpToDate, Available, Installing, Failed };
    UpdateState m_update = UpdateState::Idle;
    double m_updateTime = 0;       // when m_update last changed (status fades out)
    bool m_updateManual = false;   // "Check now": report the result as a toast
    ReleaseInfo m_release;
    std::string m_updateError;
    bool m_openUpdate = false;
    bool m_relaunch = false;
    float m_welcomeHeight = 0;     // measured last frame, to centre the welcome screen

    std::thread m_restartThread;
    std::atomic<bool> m_restarting{ false };
    std::atomic<bool> m_restartDone{ false };
    ExplorerRestartResult m_restartResult;
};
