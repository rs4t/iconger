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
#include "window_icons.h"
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

    // ---- running in the background (EXPERIMENTAL unpinned-app icons) ----
    static constexpr UINT_PTR kKeeperTimerId = 7;
    /// Closing the window only hides it: the icon keeper must keep running.
    bool KeepsRunningInBackground() const { return m_settings.unpinnedIcons; }
    /// "Quit Iconger completely" was chosen.
    bool WantsQuit() const { return m_quit; }
    /// The window was brought back from the background.
    void OnShown();
    /// Every ~2 s while the keeper runs (WM_TIMER).
    void OnKeeperTimer();

    /// What's under a point (client pixels) of the custom title bar, for WM_NCHITTEST.
    enum class TitleHit { None, Caption, Minimize, Maximize, Close };
    TitleHit HitTestTitleBar(int x, int y) const;
    /// The maximize button is pressed/released (its clicks arrive as non-client messages).
    void SetMaximizePressed(bool pressed) { m_maxPressed = pressed; }

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
        // Simple Icons: the logo and its background, so the background can be changed
        std::string brandSvg;
        uint32_t tileColor = 0;             // 0xRRGGBB (the brand colour at first)
        uint32_t brandColor = 0;
        TileShape tileShape = TileShape::RoundedSquare;
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
    void RedrawBrandTile();
    void DrawBackgroundControls();
    void CustomizeCurrentIcon();
    void HandlePickedFile(const std::wstring& path);
    void BrowseForIcon();
    void ApplyCandidate();
    void ApplyToPackagedApp(const std::wstring& iconPath, int iconIndex);
    bool SetPinnedIcon(Entry& e, const std::wstring& iconPath, int iconIndex);
    void ExportSetup();
    void ImportSetup();
    void RestoreOriginal(const std::wstring& lnkPath, bool quiet = false);
    void RestoreAll();
    void RestoreRunningApp(const std::wstring& exe);
    void SetUnpinnedIcons(bool enable);
    void RefreshRunningApps();
    void AddRunningApps();
    void RecycleLeftovers();
    void RequestRestart();
    void StartRestart();
    void PollRestart();

    // views
    void DrawSidebar(float width);
    void DrawWindowControls();
    float TitleBarHeight() const;
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
    void DrawWhatsNew();
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
    bool m_openEnableUnpinned = false;
    bool m_openImportResult = false;
    int m_importApplied = 0;
    std::vector<std::string> m_importSkipped;   // "Name: why"
    bool m_importNeedsFeature = false;         // unpinned-app icons came in with the feature off
    std::wstring m_pendingOpenKey;  // app to open once the experimental feature is on
    std::wstring m_pinGuideLnk;   // shortcut made for a packaged app, waiting to be pinned
    std::string m_pinGuideName;

    // EXPERIMENTAL: icons for running apps that aren't pinned
    WindowIconRules m_winRules;
    WindowIconKeeper m_keeper;
    bool m_quit = false;
    bool m_runningDirty = false;   // re-list running apps on the next frame
    bool m_startsWithWindows = false;

    // "What's new" after an update: this version's notes, built into the exe
    std::string m_notes;
    bool m_showWhatsNew = false;
    float m_whatsNewHeight = 0;

    enum class UpdateState { Idle, Checking, UpToDate, Available, Installing, Failed };
    UpdateState m_update = UpdateState::Idle;
    double m_updateTime = 0;       // when m_update last changed (status fades out)
    bool m_updateManual = false;   // "Check now": report the result as a toast
    ReleaseInfo m_release;
    std::string m_updateError;
    bool m_openUpdate = false;
    bool m_relaunch = false;
    float m_welcomeHeight = 0;     // measured last frame, to centre the welcome screen

    // custom title bar: geometry from the last frame (client pixels), read by WM_NCHITTEST
    float m_titleH = 0;
    ImVec4 m_ctlRect[3] = {};      // minimize, maximize, close: x0, y0, x1, y1
    int m_ctlHeld = -1;            // minimize/close pressed in the UI
    bool m_maxPressed = false;

    // animation timelines (ImGui::GetTime() when it happened)
    double m_startedAt = 0;        // app start: the sidebar menu rises in
    double m_candAt = -100;        // an icon was picked: the "New" preview pops
    double m_adjustAt = -100;      // the first icon was picked: the Adjust panel rises in
    double m_appliedAt = -100;     // an icon was applied or restored: the card and preview celebrate
    std::wstring m_appliedKey;     // ...for this pin (EntryKey)

    std::thread m_restartThread;
    std::atomic<bool> m_restarting{ false };
    std::atomic<bool> m_restartDone{ false };
    ExplorerRestartResult m_restartResult;
};
