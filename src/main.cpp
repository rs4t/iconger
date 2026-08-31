// Iconger - Windows pinned taskbar icon changer
// MIT License

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <commdlg.h>

#include "shell_link.h"
#include "taskbar_enum.h"
#include "icon_utils.h"
#include "explorer_refresh.h"
#include "ui_style.h"

#include <string>
#include <vector>
#include <cstdio>
#include <cstdarg>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

// ── Icon symbols (Unicode, render in any modern font) ─────────────────
#define ICON_REFRESH  "\xF0\x9F\x94\x84"   // U+1F504 Refresh
#define ICON_FOLDER   "\xF0\x9F\x93\x82"   // U+1F4C2 Folder
#define ICON_IMAGE    "\xF0\x9F\x96\xBC"   // U+1F5BC Image
#define ICON_CHECK    "\xE2\x9C\x93"       // U+2713 Check
#define ICON_UNDO     "\xE2\x86\xA9"       // U+21A9 Undo
#define ICON_LIST     "\xF0\x9F\x93\x8B"   // U+1F4CB Clipboard
#define ICON_EYE      "\xF0\x9F\x91\x81"   // U+1F441 Eye
#define ICON_WARNING  "\xE2\x9A\xA0"       // U+26A0 Warning
#define ICON_DOCUMENT "\xF0\x9F\x93\x84"   // U+1F4C4 Document
#define ICON_GEAR     "\xE2\x9A\x99"       // U+2699 Gear

static HWND g_hwnd = nullptr;
static ID3D11Device* g_d3dDevice = nullptr;
static ID3D11DeviceContext* g_d3dDeviceContext = nullptr;
static IDXGISwapChain* g_swapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static std::vector<TaskbarButton> g_shortcuts;
static int g_selectedIndex = -1;
static HICON g_previewIcon = nullptr;
static ID3D11ShaderResourceView* g_previewTexture = nullptr;
static std::wstring g_newIconPath;
static int g_newIconIndex = 0;
static std::vector<std::string> g_messages;
static std::vector<IconInfo> g_browserIcons;
static int g_pickedIconIndex = -1;

static void Log(const char* fmt, ...)
{
    va_list args; va_start(args, fmt);
    char buf[1024] = {};
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    g_messages.push_back(buf);
    if (g_messages.size() > 100) g_messages.erase(g_messages.begin());
}

static bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL out = D3D_FEATURE_LEVEL_11_0;
    return SUCCEEDED(D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        levels, (UINT)std::size(levels), D3D11_SDK_VERSION,
        &sd, &g_swapChain, &g_d3dDevice, &out, &g_d3dDeviceContext));
}

static void CreateRenderTarget()
{
    ID3D11Texture2D* bb = nullptr;
    if (SUCCEEDED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&bb)))) {
        g_d3dDevice->CreateRenderTargetView(bb, nullptr, &g_mainRenderTargetView);
        bb->Release();
    }
}

static void CleanupDeviceD3D()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
    if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
    if (g_d3dDeviceContext) { g_d3dDeviceContext->Release(); g_d3dDeviceContext = nullptr; }
    if (g_d3dDevice) { g_d3dDevice->Release(); g_d3dDevice = nullptr; }
}

static void FreeShortcutIcons() { for (auto& sc : g_shortcuts) FreeIcon(sc.hIcon); }
static void LoadShortcutIcons()
{
    for (auto& sc : g_shortcuts) {
        if (!sc.hIcon) { sc.hIcon = ExtractSingleIcon(sc.exePath, 0); }
    }
}
static void FreePreviewResources()
{
    if (g_previewTexture) { g_previewTexture->Release(); g_previewTexture = nullptr; }
    FreeIcon(g_previewIcon);
}
static void ClearPreviewIcon() { FreePreviewResources(); g_newIconPath.clear(); g_newIconIndex = 0; }
static void LoadPreviewIcon(const std::wstring& path, int index)
{
    g_previewIcon = ExtractSingleIcon(path, index);
    if (g_previewIcon && g_d3dDevice)
        g_previewTexture = CreateTextureFromHICON(g_d3dDevice, g_previewIcon);
}
static void FreeBrowserIcons()
{
    for (auto& ic : g_browserIcons) FreeIcon(ic.hIcon);
    g_browserIcons.clear(); g_pickedIconIndex = -1;
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (g_d3dDevice && wParam != SIZE_MINIMIZED) {
            if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
            g_swapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ── UTF-8 conversion helper ─────────────────────────────────────────
static std::string WideToUTF8(const wchar_t* wstr)
{
    if (!wstr || !*wstr) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], len, nullptr, nullptr);
    return result;
}
// ── WinMain ───────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"IcongerWindowClass";
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(0, L"IcongerWindowClass",
        L"Iconger",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 860, 620,
        nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return 1;
    if (!CreateDeviceD3D(g_hwnd)) { CleanupDeviceD3D(); return 1; }
    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // ── Load font: Segoe UI (system font) ─────────────────────────────
    ImFontConfig cfg;
    cfg.SizePixels = 14.0f;
    io.FontDefault = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\segoeui.ttf", 14.0f, &cfg);
    if (!io.FontDefault)
        io.FontDefault = io.Fonts->AddFontDefault(&cfg);

    ui::SetupStyle();
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_d3dDevice, g_d3dDeviceContext);

    Log("Enumerating taskbar buttons...");
    g_shortcuts = EnumerateTaskbarButtons();
    Log("Found %zu taskbar button(s).", g_shortcuts.size());
    LoadShortcutIcons();

    std::vector<ID3D11ShaderResourceView*> shortcutTextures(g_shortcuts.size(), nullptr);

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) done = true;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (done) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // ── Full-viewport background window ────────────────────────────
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::Begin("IcongerMain", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopStyleVar();

        // ── Header bar (52px, full width) ──────────────────────────────
        {
            const float headerH = 52.0f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetWindowPos();
            float w = ImGui::GetWindowWidth();

            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + headerH), ui::c_header_bg, 0);
            dl->AddRectFilled(ImVec2(p.x, p.y + headerH - 2),
                ImVec2(p.x + w, p.y + headerH), ui::c_accent, 0);

            // Title: icon + text, centered vertically
            float titleY = (headerH - ImGui::GetFontSize()) * 0.5f;
            ImGui::SetCursorPos(ImVec2(20, titleY));
            ImGui::PushStyleColor(ImGuiCol_Text, ui::c_accent);
            ImGui::TextUnformatted(ICON_GEAR);
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 8);
            ImGui::SetCursorPosY(titleY);
            ImGui::PushStyleColor(ImGuiCol_Text, ui::c_text_bright);
            ImGui::TextUnformatted("Iconger");
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 10);
            ImGui::SetCursorPosY(titleY + 1);
            ImGui::PushStyleColor(ImGuiCol_Text, ui::c_text_muted);
            ImGui::TextUnformatted("Taskbar Icon Changer");
            ImGui::PopStyleColor();

            // Refresh button (right-aligned)
            ImGui::SameLine(0, 0);
            ImGui::SetCursorPos(ImVec2(w - 110, (headerH - 28) * 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Button, ui::c_card_bg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::c_card_hover);
            if (ImGui::Button("..", ImVec2(28, 28))) {
                FreeShortcutIcons();
                for (auto& t : shortcutTextures) if (t) { t->Release(); t = nullptr; }
                shortcutTextures.clear();
                g_shortcuts = EnumerateTaskbarButtons();
                LoadShortcutIcons();
                shortcutTextures.resize(g_shortcuts.size(), nullptr);
                g_selectedIndex = -1;
                ClearPreviewIcon();
                FreeBrowserIcons();
                Log("Taskbar refreshed.");
            }
            ImGui::PopStyleColor();
            ImGui::PopStyleColor();

            ImGui::SetCursorPosY(headerH + 10);
        }

        // ── Split layout: left panel + right panel ────────────────────
        ImGui::Columns(2, "columns", true);
        ImGui::SetColumnWidth(0, 300.0f);

        // ── LEFT PANEL ────────────────────────────────────────────────
        {
            ImGui::BeginChild("ListPanel", ImVec2(0, -50), false,
                ImGuiWindowFlags_AlwaysUseWindowPadding);

            // Section header with icon
            ImGui::SetCursorPos(ImVec2(12, 4));
            ImGui::PushStyleColor(ImGuiCol_Text, ui::c_accent);
            ImGui::TextUnformatted(ICON_LIST);
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 6);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
            ImGui::PushStyleColor(ImGuiCol_Text, ui::c_text_dim);
            ImGui::TextUnformatted("Pinned Apps");
            ImGui::PopStyleColor();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::BeginChild("ListInner", ImVec2(0, 0), false);

            for (int i = 0; i < (int)g_shortcuts.size(); ++i) {
                const auto& sc = g_shortcuts[i];
                ImGui::PushID(i);

                if (!shortcutTextures[i] && sc.hIcon && g_d3dDevice)
                    shortcutTextures[i] = CreateTextureFromHICON(g_d3dDevice, sc.hIcon);

                bool isSelected = (g_selectedIndex == i);
                ImVec2 itemMin = ImGui::GetCursorScreenPos();
                float itemW = ImGui::GetContentRegionAvail().x;

                ImGui::InvisibleButton("item", ImVec2(itemW, 36));
                if (ImGui::IsItemClicked()) {
                    g_selectedIndex = i;
                    ClearPreviewIcon();
                }

                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 itemMax = ImVec2(itemMin.x + itemW, itemMin.y + 36);
                if (isSelected) {
                    dl->AddRectFilled(itemMin, itemMax, ui::c_accent_dim, 6.0f);
                    dl->AddRectFilled(ImVec2(itemMin.x, itemMin.y),
                        ImVec2(itemMin.x + 3, itemMax.y), ui::c_accent, 1.5f);
                } else if (ImGui::IsItemHovered()) {
                    dl->AddRectFilled(itemMin, itemMax, ui::c_card_hover, 6.0f);
                }

                float iconY = itemMin.y + (36 - 24) * 0.5f;
                if (shortcutTextures[i]) {
                    dl->AddImage((ImTextureID)(intptr_t)shortcutTextures[i],
                        ImVec2(itemMin.x + 8, iconY),
                        ImVec2(itemMin.x + 32, iconY + 24));
                }

                float textX = itemMin.x + 40;
                float textY = itemMin.y + (36 - ImGui::GetFontSize()) * 0.5f;
                ImU32 textColor = !sc.hasEditableLnk ? ui::c_warning
                    : isSelected ? ui::c_text_bright : ui::c_text;
                dl->AddText(ImVec2(textX, textY), textColor,
                    WideToUTF8(sc.displayName.c_str()).c_str());

                ImGui::PopID();
            }

            ImGui::EndChild();
            ImGui::EndChild();
        }

        ImGui::NextColumn();
        // ── RIGHT PANEL ──────────────────────────────────────────────
        {
            ImGui::BeginChild("DetailPanel", ImVec2(0, -50), false,
                ImGuiWindowFlags_AlwaysUseWindowPadding);

            // Section header
            ImGui::SetCursorPos(ImVec2(12, 4));
            ImGui::PushStyleColor(ImGuiCol_Text, ui::c_accent);
            ImGui::TextUnformatted(ICON_EYE);
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 6);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
            ImGui::PushStyleColor(ImGuiCol_Text, ui::c_text_dim);
            ImGui::TextUnformatted("Details & Preview");
            ImGui::PopStyleColor();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
            ImGui::Separator();
            ImGui::Spacing();

            if (g_selectedIndex >= 0 && g_selectedIndex < (int)g_shortcuts.size()) {
                const auto& sc2 = g_shortcuts[g_selectedIndex];

                // ── Info card ─────────────────────────────────────────
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::c_card_bg);
                ImGui::BeginChild("InfoCard", ImVec2(0, 82), true,
                    ImGuiWindowFlags_AlwaysUseWindowPadding);
                ImGui::SetCursorPos(ImVec2(10, 8));

                if (shortcutTextures[g_selectedIndex]) {
                    ImGui::Image(
                        (ImTextureID)(intptr_t)shortcutTextures[g_selectedIndex],
                        ImVec2(48, 48));
                    ImGui::SameLine(0, 12);
                }

                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
                ImGui::PushStyleColor(ImGuiCol_Text, ui::c_text_bright);
                ImGui::TextUnformatted(WideToUTF8(sc2.displayName.c_str()).c_str());
                ImGui::PopStyleColor();

                ImGui::PushStyleColor(ImGuiCol_Text, ui::c_text_muted);
                ImGui::TextWrapped("%s", WideToUTF8(sc2.exePath.c_str()).c_str());
                ImGui::PopStyleColor();

                if (sc2.hasEditableLnk) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::c_text_dim);
                    ImGui::Text("Shortcut: %s", WideToUTF8(sc2.lnkPath.c_str()).c_str());
                    ImGui::PopStyleColor();
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::c_warning);
                    // icon removed
                    ImGui::TextUnformatted("No matching .lnk - read only");
                    ImGui::PopStyleColor();
                }

                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::Spacing();

                // ── Icon source selection ─────────────────────────────
                ImGui::PushStyleColor(ImGuiCol_Text, ui::c_text_dim);
                ImGui::TextUnformatted("Select icon source:");
                ImGui::PopStyleColor();
                ImGui::Spacing();

                // Icon buttons row (centered)
                float availW = ImGui::GetContentRegionAvail().x;
                float btnStartX = (availW - 108) * 0.5f;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + btnStartX);

                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
                if (ImGui::Button("..", ImVec2(32, 32))) {
                    wchar_t buf[MAX_PATH] = {};
                    OPENFILENAMEW ofn = {};
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = g_hwnd;
                    ofn.lpstrFilter = L"Icon files\0*.ico\0All\0*.*\0";
                    ofn.lpstrFile = buf; ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                    if (GetOpenFileNameW(&ofn)) {
                        FreeBrowserIcons(); ClearPreviewIcon();
                        g_newIconPath = buf; g_newIconIndex = 0;
                        LoadPreviewIcon(buf, 0);
                        Log("Selected .ico: %s", WideToUTF8(buf).c_str());
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(".ico file");

                ImGui::SameLine(0, 6);

                if (ImGui::Button("..", ImVec2(32, 32))) {
                    wchar_t buf[MAX_PATH] = {};
                    OPENFILENAMEW ofn = {};
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = g_hwnd;
                    ofn.lpstrFilter = L"EXE/DLL\0*.exe;*.dll\0All\0*.*\0";
                    ofn.lpstrFile = buf; ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                    if (GetOpenFileNameW(&ofn)) {
                        FreeBrowserIcons();
                        g_browserIcons = ExtractIconsFromFile(buf, 200);
                        if (!g_browserIcons.empty()) {
                            g_pickedIconIndex = -1;
                            Log("Found %zu icon(s) in %s", g_browserIcons.size(), WideToUTF8(buf).c_str());
                        }
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(".exe / .dll");

                ImGui::SameLine(0, 6);

                if (ImGui::Button("..", ImVec2(32, 32))) {
                    wchar_t buf[MAX_PATH] = {};
                    OPENFILENAMEW ofn = {};
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = g_hwnd;
                    ofn.lpstrFilter = L"PNG\0*.png\0All\0*.*\0";
                    ofn.lpstrFile = buf; ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                    if (GetOpenFileNameW(&ofn)) {
                        FreeBrowserIcons();
                        std::wstring icoPath = ConvertPNGToICO(buf);
                        if (!icoPath.empty()) {
                            ClearPreviewIcon();
                            g_newIconPath = icoPath; g_newIconIndex = 0;
                            LoadPreviewIcon(icoPath, 0);
                            Log("PNG -> ICO: %s", WideToUTF8(icoPath.c_str()).c_str());
                        } else {
                            Log("Failed to convert: %s", WideToUTF8(buf).c_str());
                        }
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(".png (auto-convert)");
                ImGui::PopStyleVar();

                ImGui::SameLine(0, 6);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.5f, 1));
                ImGui::TextUnformatted(".ico    .exe/.dll    .png");
                ImGui::PopStyleColor();

                ImGui::Spacing();

                // ── Icon browser grid ─────────────────────────────────
                if (!g_browserIcons.empty()) {
                    ImGui::Separator();
                    ImGui::Spacing();
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::c_text_dim);
                    ImGui::TextUnformatted("Click an icon to select:");
                    ImGui::PopStyleColor();
                    ImGui::BeginChild("IconGrid", ImVec2(0, 140), true);
                    int cols = (std::max)(1, (int)(ImGui::GetWindowWidth() / 36));
                    for (int i = 0; i < (int)g_browserIcons.size(); ++i) {
                        if (i % cols != 0) ImGui::SameLine();
                        ImGui::PushID(i);
                        ID3D11ShaderResourceView* tex = CreateTextureFromHICON(g_d3dDevice, g_browserIcons[i].hIcon);
                        bool isActive = (i == g_pickedIconIndex);
                        if (tex) {
                            if (ImGui::ImageButton((ImTextureID)(intptr_t)tex, ImVec2(28, 28),
                                ImVec2(0,0), ImVec2(1,1), 1)) {
                                ClearPreviewIcon();
                                g_newIconPath = g_browserIcons[i].sourcePath;
                                g_newIconIndex = g_browserIcons[i].index;
                                g_pickedIconIndex = i;
                                LoadPreviewIcon(g_browserIcons[i].sourcePath, g_browserIcons[i].index);
                                Log("Picked icon #%d [index %d]", i, g_browserIcons[i].index);
                            }
                            tex->Release();
                        } else {
                            if (ImGui::Button(std::to_string(i).c_str(), ImVec2(28, 28))) {
                                ClearPreviewIcon();
                                g_newIconPath = g_browserIcons[i].sourcePath;
                                g_newIconIndex = g_browserIcons[i].index;
                                g_pickedIconIndex = i;
                                LoadPreviewIcon(g_browserIcons[i].sourcePath, g_browserIcons[i].index);
                                Log("Picked icon #%d [index %d]", i, g_browserIcons[i].index);
                            }
                        }
                        if (isActive) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0,1,0,1), "<"); }
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                    ImGui::Spacing();
                }

                // ── Preview card ──────────────────────────────────────
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::c_card_bg);
                ImGui::BeginChild("PreviewCard", ImVec2(0, 76), true,
                    ImGuiWindowFlags_AlwaysUseWindowPadding);
                ImGui::SetCursorPos(ImVec2(10, 8));

                if (g_previewTexture) {
                    ImGui::Image((ImTextureID)(intptr_t)g_previewTexture, ImVec2(48, 48));
                    ImGui::SameLine(0, 12);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8);
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::c_text_bright);
                    ImGui::TextUnformatted("New icon preview");
                    ImGui::PopStyleColor();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.45f, 1));
                    ImGui::Text("Index: %d", g_newIconIndex);
                    ImGui::PopStyleColor();
                } else {
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 14);
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::c_text_muted);
                    ImGui::TextUnformatted("No icon selected yet");
                    ImGui::PopStyleColor();
                }

                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::Spacing();

                // ── Action buttons ────────────────────────────────────
                ImGui::Separator();
                ImGui::Spacing();

                if (sc2.hasEditableLnk) {
                    float btnAreaW = ImGui::GetContentRegionAvail().x;
                    float btnGrpW = (float)(g_newIconPath.empty() ? 160 : 310);
                    float btnStartX2 = (btnAreaW - btnGrpW) * 0.5f;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + btnStartX2);

                    if (!g_newIconPath.empty()) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ui::c_card_bg);
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::c_card_hover);
                        // check icon removed
                        ImGui::PopStyleColor();
                        ImGui::PopStyleColor();
                        ImGui::SameLine(0, 4);
                        if (ImGui::Button("Apply Icon", ImVec2(120, 30))) {
                            bool ok = SetShortcutIcon(sc2.lnkPath, g_newIconPath, g_newIconIndex);
                            if (ok) {
                                Log("Icon applied to %s", WideToUTF8(sc2.displayName.c_str()).c_str());
                                SignalIconChange();
                                if (ConfirmNuclearRefresh(g_hwnd)) {
                                    NuclearRefresh(); Log("Explorer restart issued.");
                                } else {
                                    Log("Refresh skipped; changes need restart.");
                                }
                            } else {
                                Log("FAILED to apply icon to %s", WideToUTF8(sc2.displayName.c_str()).c_str());
                            }
                        }
                        ImGui::SameLine(0, 8);
                    }

                    ImGui::PushStyleColor(ImGuiCol_Button, ui::c_card_bg);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::c_card_hover);
                    // undo icon removed
                    ImGui::PopStyleColor();
                    ImGui::PopStyleColor();
                    ImGui::SameLine(0, 4);
                    if (ImGui::Button("Reset to Default", ImVec2(120, 30))) {
                        bool ok = ResetShortcutIcon(sc2.lnkPath);
                        if (ok) {
                            Log("Icon reset to default for %s", WideToUTF8(sc2.displayName.c_str()).c_str());
                            SignalIconChange();
                            if (ConfirmNuclearRefresh(g_hwnd)) {
                                NuclearRefresh(); Log("Explorer restart issued.");
                            } else {
                                Log("Refresh skipped; changes need restart.");
                            }
                        } else {
                            Log("FAILED to reset icon for %s", WideToUTF8(sc2.displayName.c_str()).c_str());
                        }
                    }
                } else {
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::c_text_muted);
                    ImGui::TextWrapped("This taskbar entry has no matching .lnk file.");
                    ImGui::PopStyleColor();
                }
            } else {
                // No selection placeholder - centered
                float availW = ImGui::GetContentRegionAvail().x;
                float availH = ImGui::GetContentRegionAvail().y;
                ImGui::SetCursorPos(ImVec2(availW * 0.5f - 100, availH * 0.5f - 20));
                ImGui::PushStyleColor(ImGuiCol_Text, ui::c_text_muted);
                ImGui::TextUnformatted("Select a taskbar entry from the list");
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
                ImGui::SetCursorPosX(availW * 0.5f - 90);
                ImGui::TextUnformatted("to view details and change its icon.");
                ImGui::PopStyleColor();
            }

            ImGui::EndChild();
        }

        // ── Footer log bar ────────────────────────────────────────────
        ImGui::NextColumn();
        ImGui::Columns(1);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::c_log_bg);
        ImGui::BeginChild("LogPanel", ImVec2(0, 34), false);
        ImGui::SetCursorPos(ImVec2(12, 8));
        ImGui::PushStyleColor(ImGuiCol_Text, ui::c_text_dim);
        if (!g_messages.empty()) {
            ImGui::TextUnformatted(g_messages.back().c_str());
        }
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::End(); // IcongerMain

        // ── Render ────────────────────────────────────────────────────
        ImGui::Render();
        g_d3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        float cc[4] = { 0.10f, 0.10f, 0.12f, 1.00f };
        g_d3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, cc);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    FreeShortcutIcons();
    for (auto& t : shortcutTextures) if (t) t->Release();
    ClearPreviewIcon();
    FreeBrowserIcons();
    CleanupDeviceD3D();
    CoUninitialize();
    return 0;
} // WinMain



