// Iconger - change the icons of pinned taskbar apps on Windows 10/11
// MIT License

#include "app.h"
#include "gfx.h"
#include "ui/theme.h"
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static App g_app;
static bool g_ready = false;

// Paint the native title bar in the app's background colour (Windows 11) and
// use the dark variant elsewhere, so the window reads as one surface.
static void StyleTitleBar(HWND hwnd)
{
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    COLORREF caption = RGB(12, 18, 31), textCol = RGB(240, 244, 248);
    DwmSetWindowAttribute(hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, 36 /*DWMWA_TEXT_COLOR*/, &textCol, sizeof(textCol));
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
        if (HWND other = FindWindowW(L"IcongerWindow", nullptr)) {
            ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(12, 18, 31)); // no white flash before the first frame
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
                 SWP_NOZORDER | SWP_NOACTIVATE);
    StyleTitleBar(hwnd);

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
    int argc = 0;
    if (wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        g_app.ApplyCommandLine(argc, argv);
        LocalFree(argv);
    }
    g_ready = true;
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Render only while something happens: a few frames after each input so
    // hover/press states settle, continuously while loading or animating,
    // otherwise sleep until the next message (the old loop burned a core at 60 fps).
    int framesToRender = 3;
    bool running = true;
    while (running) {
        bool busy = framesToRender > 0 || g_app.IsBusy() || ImGui::GetIO().WantTextInput;
        if (!busy) MsgWaitForMultipleObjects(0, nullptr, FALSE, 500, QS_ALLINPUT);

        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            framesToRender = 3;
        }
        if (!running) break;
        if (IsIconic(hwnd)) { Sleep(50); continue; }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        g_app.Frame();
        ImGui::Render();

        static const float clear[4] = { 12 / 255.f, 18 / 255.f, 31 / 255.f, 1 };
        GfxRender(clear);
        if (!GfxPresent()) Sleep(50);

        if (framesToRender > 0) --framesToRender;
        if (ImGui::IsAnyItemActive()) framesToRender = 3; // dragging a scrollbar etc.
    }

    g_app.Shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    GfxShutdown();
    DestroyWindow(hwnd);
    CoUninitialize();
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
