// Iconger - change the icons of pinned taskbar apps on Windows 10/11
// MIT License

#include "app.h"
#include "gfx.h"
#include "ui/theme.h"
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>
#include <algorithm>
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static App g_app;
static bool g_ready = false;

// A second launch asks the running instance to show itself (it may be hidden in the background).
static const UINT WM_APP_SHOW = WM_APP + 1;
static const UINT_PTR kKeeperTimer = App::kKeeperTimerId;

// The window draws its own title bar (see WM_NCCALCSIZE below). Keep the dark
// variant for anything Windows still draws (borders, system menu), and on Windows 11
// colour the 1 px border to match the app.
static void StyleFrame(HWND hwnd)
{
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    COLORREF borderCol = RGB(44, 39, 51); // theme::border
    DwmSetWindowAttribute(hwnd, 34 /*DWMWA_BORDER_COLOR*/, &borderCol, sizeof(borderCol));
}

// Size of the invisible resize border Windows puts around a sizable window.
static POINT FrameSize(HWND hwnd)
{
    UINT dpi = GetDpiForWindow(hwnd);
    int pad = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    return { GetSystemMetricsForDpi(SM_CXFRAME, dpi) + pad, GetSystemMetricsForDpi(SM_CYFRAME, dpi) + pad };
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) GfxResize(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_GETMINMAXINFO: {
        float scale = (float)GetDpiForWindow(hwnd) / 96.0f;
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize = { (LONG)(820 * scale), (LONG)(560 * scale) };
        return 0;
    }
    case WM_DPICHANGED: {
        const RECT* r = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        if (g_ready) g_app.SetDpiScale(HIWORD(wParam) / 96.0f);
        return 0;
    }
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        std::vector<std::wstring> files;
        UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n; ++i) {
            UINT len = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring f(len, L'\0');
            DragQueryFileW(drop, i, f.data(), len + 1);
            files.push_back(f);
        }
        DragFinish(drop);
        if (g_ready) g_app.OnFilesDropped(files);
        return 0;
    }
    case WM_NCCALCSIZE:
        // Take over the title bar: the client area extends to the top edge, but keeps
        // Windows' resize borders on the other sides (and so the shadow, snapping and
        // rounded corners of a normal window).
        if (wParam) {
            RECT& r = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam)->rgrc[0];
            POINT f = FrameSize(hwnd);
            r.left += f.x;
            r.right -= f.x;
            r.bottom -= f.y;
            if (IsZoomed(hwnd)) r.top += f.y; // maximized windows hang over the screen edge by the frame size
            return 0;
        }
        break;
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam); // side and bottom borders
        if (hit != HTCLIENT) return hit;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        if (!IsZoomed(hwnd)) {
            // the top resize border now lies inside our client area
            RECT rc;
            GetClientRect(hwnd, &rc);
            POINT f = FrameSize(hwnd);
            int top = std::max(2, (int)(f.y * 0.6f));
            if (pt.y < top) return pt.x < f.x * 2 ? HTTOPLEFT : pt.x >= rc.right - f.x * 2 ? HTTOPRIGHT : HTTOP;
        }
        if (g_ready) {
            switch (g_app.HitTestTitleBar(pt.x, pt.y)) {
            case App::TitleHit::Caption:  return HTCAPTION;   // drag, double-click, right-click menu
            case App::TitleHit::Maximize: return HTMAXBUTTON; // lets Windows 11 show Snap Layouts
            default: break;
            }
        }
        return HTCLIENT;
    }
    // The maximize button counts as non-client (for Snap Layouts), so its clicks come
    // here instead of to the UI. Eat them, or Windows paints its own classic button.
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK:
        if (wParam == HTMAXBUTTON) {
            if (g_ready) g_app.SetMaximizePressed(true);
            return 0;
        }
        break;
    case WM_NCLBUTTONUP:
        if (wParam == HTMAXBUTTON) {
            if (g_ready) g_app.SetMaximizePressed(false);
            ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
            return 0;
        }
        break;
    case WM_NCMOUSELEAVE:
        if (g_ready) g_app.SetMaximizePressed(false);
        break;
    case WM_CLOSE:
        // With custom icons for unpinned apps on, Iconger keeps running without a window
        if (g_ready && g_app.KeepsRunningInBackground() && !g_app.WantsQuit()) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        break;
    case WM_APP_SHOW:
        ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(hwnd);
        if (g_ready) g_app.OnShown();
        return 0;
    case WM_TIMER:
        if (wParam == kKeeperTimer && g_ready) g_app.OnKeeperTimer();
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0; // Alt would freeze the loop in the menu
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow)
{
    // Single instance: focus the running window instead of opening a second one.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\Iconger.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // the other instance may still be starting up and not have its window yet
        HWND other = FindWindowW(L"IcongerWindow", nullptr);
        for (int i = 0; i < 30 && !other; ++i) {
            Sleep(100);
            other = FindWindowW(L"IcongerWindow", nullptr);
        }
        if (other) {
            // let it take the foreground (it may be a background process without a window)
            DWORD pid = 0;
            GetWindowThreadProcessId(other, &pid);
            AllowSetForegroundWindow(pid);
            PostMessageW(other, WM_APP_SHOW, 0, 0);
        }
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(theme::bgColorRef); // no white flash before the first frame
    wc.lpszClassName = L"IcongerWindow";
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName, L"Iconger", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1120, 740, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;

    // Now that the window exists it knows its monitor's DPI (the manifest makes us
    // per-monitor aware): size it for that, centred in the work area.
    float scale = GetDpiForWindow(hwnd) / 96.0f;
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    int w = std::min((int)(1120 * scale), (int)(mi.rcWork.right - mi.rcWork.left));
    int h = std::min((int)(740 * scale), (int)(mi.rcWork.bottom - mi.rcWork.top));
    SetWindowPos(hwnd, nullptr, mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2,
                 mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2, w, h,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED); // FRAMECHANGED: apply WM_NCCALCSIZE
    StyleFrame(hwnd);

    if (!GfxInit(hwnd)) {
        MessageBoxW(hwnd, L"Could not initialise Direct3D 11.", L"Iconger", MB_ICONERROR);
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // the old build dropped an imgui.ini in whatever folder it was started from
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    theme::LoadFonts();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(GfxDevice(), GfxContext());

    g_app.Init(hwnd, ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd));
    bool background = false; // started with Windows: run without showing the window
    int argc = 0;
    if (wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i) background |= wcscmp(argv[i], L"--background") == 0;
        g_app.ApplyCommandLine(argc, argv);
        LocalFree(argv);
    }
    g_ready = true;
    if (background && !g_app.KeepsRunningInBackground()) {
        g_app.Shutdown(); // the feature was turned off since: nothing to do at login
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        GfxShutdown();
        DestroyWindow(hwnd);
        CoUninitialize();
        return 0;
    }
    if (!background) {
        ShowWindow(hwnd, nCmdShow);
        UpdateWindow(hwnd);
    }

    // Render only while something happens: a few frames after each input so
    // hover/press states settle, continuously while loading or animating,
    // otherwise sleep until the next message (the old loop burned a core at 60 fps).
    int framesToRender = 3;
    bool running = true;
    while (running) {
        // Hidden (running in the background) or minimized: nothing to draw, so sleep until a
        // message arrives. Window events for the icon keeper come in as messages too.
        const bool offscreen = !IsWindowVisible(hwnd) || IsIconic(hwnd);
        bool busy = framesToRender > 0 || g_app.IsBusy() || ImGui::GetIO().WantTextInput;
        if (offscreen) MsgWaitForMultipleObjects(0, nullptr, FALSE, INFINITE, QS_ALLINPUT);
        else if (!busy) MsgWaitForMultipleObjects(0, nullptr, FALSE, 500, QS_ALLINPUT);

        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            framesToRender = 3;
        }
        if (!running) break;
        if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) continue;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        g_app.Frame();
        ImGui::Render();

        static const float clear[4] = { 17 / 255.f, 15 / 255.f, 20 / 255.f, 1 };
        GfxRender(clear);
        if (!GfxPresent()) Sleep(50);

        if (framesToRender > 0) --framesToRender;
        if (ImGui::IsAnyItemActive()) framesToRender = 3; // dragging a scrollbar etc.
        if (g_app.WantsRelaunch() || g_app.WantsQuit()) running = false;
    }
    const bool relaunch = g_app.WantsRelaunch();

    g_app.Shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    GfxShutdown();
    DestroyWindow(hwnd);
    CoUninitialize();
    ReleaseMutex(mutex);
    CloseHandle(mutex);

    // An update replaced iconger.exe: start the new one. Only now, with the single-instance
    // mutex released, or it would just focus this (closing) window and quit.
    if (relaunch) {
        wchar_t exe[MAX_PATH * 4];
        DWORD n = GetModuleFileNameW(nullptr, exe, (DWORD)std::size(exe));
        std::wstring cmd = L"\"" + std::wstring(exe, n) + L"\"";
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        if (CreateProcessW(exe, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
    return 0;
}
