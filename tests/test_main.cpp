// Iconger core tests. Plain asserts, no framework: returns non-zero on failure.
// Everything runs in a temp folder; the real pinned folder is never touched.
#include "app_paths.h"
#include "icon_adjust.h"
#include "icon_backup.h"
#include "icon_utils.h"
#include "online_icons.h"
#include "shell_link.h"
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <stb_image_write.h>

using Microsoft::WRL::ComPtr;

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

static std::wstring g_tmp;

static bool HasOpaquePixel(const Image& img)
{
    for (size_t i = 3; i < img.rgba.size(); i += 4) if (img.rgba[i] > 200) return true;
    return false;
}

static void TestExtractSystemIcon()
{
    Image img;
    CHECK(LoadIconImage(ExpandEnv(L"%SystemRoot%\\System32\\shell32.dll"), 3, 64, img));
    CHECK(img.w == 64 && img.h == 64);
    CHECK(HasOpaquePixel(img)); // the old HICON->texture path produced alpha 0 everywhere
    CHECK(CountIcons(ExpandEnv(L"%SystemRoot%\\System32\\shell32.dll")) > 100);
}

static void TestSquareResize()
{
    Image wide;
    wide.w = 40; wide.h = 20;
    wide.rgba.assign(40 * 20 * 4, 255);
    Image sq = MakeSquareResized(wide, 16);
    CHECK(sq.w == 16 && sq.h == 16);
    CHECK(sq.rgba[3] == 0);                          // top-left is padding
    CHECK(sq.rgba[(8 * 16 + 8) * 4 + 3] == 255);     // centre is image
}

static void TestImportPngUnicodePath()
{
    // red 300x200 PNG with a non-ASCII name (stbi_load(path) used to fail on these)
    std::vector<uint8_t> px(300 * 200 * 4);
    for (size_t i = 0; i < px.size(); i += 4) { px[i] = 255; px[i + 1] = 0; px[i + 2] = 0; px[i + 3] = 255; }
    std::wstring png = g_tmp + L"\\тест-ïcon.png";
    CHECK(stbi_write_png(WideToUtf8(png).c_str(), 300, 200, 4, px.data(), 300 * 4) != 0);

    std::string err;
    std::wstring ico = ImportIconFile(png, err);
    CHECK(!ico.empty());
    CHECK(ico.rfind(IconsDir(), 0) == 0);

    for (int size : { 16, 32, 48, 256 }) {
        HICON h = (HICON)LoadImageW(nullptr, ico.c_str(), IMAGE_ICON, size, size, LR_LOADFROMFILE);
        CHECK(h != nullptr);
        if (h) DestroyIcon(h);
    }
    Image back;
    CHECK(LoadIconImage(ico, 0, 48, back));
    CHECK(back.w == 48);
    size_t mid = ((size_t)24 * 48 + 24) * 4;
    CHECK(back.rgba.size() > mid && back.rgba[mid] > 200 && back.rgba[mid + 1] < 50);

    // importing the same content again reuses the file
    CHECK(ImportIconFile(png, err) == ico);
    CHECK(ImportIconFile(g_tmp + L"\\missing.png", err).empty());
}

static std::wstring MakeShortcut(const std::wstring& name, const wchar_t* iconLoc, int iconIdx)
{
    ComPtr<IShellLinkW> link;
    CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
    link->SetPath(ExpandEnv(L"%SystemRoot%\\System32\\notepad.exe").c_str());
    if (iconLoc) link->SetIconLocation(iconLoc, iconIdx);
    ComPtr<IPersistFile> file;
    link.As(&file);
    std::wstring path = g_tmp + L"\\" + name + L".lnk";
    file->Save(path.c_str(), TRUE);
    return path;
}

static DWORD LinkFlags(const std::wstring& lnk)
{
    ComPtr<IShellLinkW> link;
    CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
    ComPtr<IPersistFile> file;
    link.As(&file);
    file->Load(lnk.c_str(), STGM_READ);
    ComPtr<IShellLinkDataList> data;
    link.As(&data);
    DWORD flags = 0;
    data->GetFlags(&flags);
    return flags;
}

static void TestShortcutIconRoundTrip()
{
    const wchar_t* envIcon = L"%SystemRoot%\\System32\\shell32.dll";
    std::wstring lnk = MakeShortcut(L"Env Icon App", envIcon, 12);

    PinnedShortcut sc;
    CHECK(ReadShortcut(lnk, sc));
    CHECK(sc.displayName == L"Env Icon App");
    CHECK(sc.iconIndex == 12);
    CHECK(_wcsicmp(ExpandEnv(sc.iconPath).c_str(), ExpandEnv(envIcon).c_str()) == 0);
    CHECK(sc.targetPath.find(L"notepad.exe") != std::wstring::npos);

    std::wstring newIcon = ExpandEnv(L"%SystemRoot%\\System32\\imageres.dll");
    CHECK(SetShortcutIcon(lnk, newIcon, 5));
    CHECK(ReadShortcut(lnk, sc));
    CHECK(_wcsicmp(ExpandEnv(sc.iconPath).c_str(), newIcon.c_str()) == 0);
    CHECK(sc.iconIndex == 5);
    CHECK(!(LinkFlags(lnk) & SLDF_HAS_EXP_ICON_SZ)); // a plain path drops the %VARS% block

    // restoring a backed-up "%VARS%" path gives back exactly that text, not its expansion
    CHECK(SetShortcutIcon(lnk, envIcon, 12));
    CHECK(ReadShortcut(lnk, sc));
    CHECK(sc.iconPath == envIcon && sc.iconIndex == 12);
    CHECK(LinkFlags(lnk) & SLDF_HAS_EXP_ICON_SZ);
    CHECK(SetShortcutIcon(lnk, newIcon, 5));
    CHECK(ReadShortcut(lnk, sc));

    std::wstring resolved; int idx = -1;
    ResolveShortcutIcon(sc, resolved, idx);
    CHECK(_wcsicmp(resolved.c_str(), newIcon.c_str()) == 0 && idx == 5);

    // clearing the override falls back to the target
    CHECK(SetShortcutIcon(lnk, L"", 0));
    CHECK(ReadShortcut(lnk, sc));
    CHECK(sc.iconPath.empty());
    CHECK(!(LinkFlags(lnk) & SLDF_HAS_EXP_ICON_SZ));
    ResolveShortcutIcon(sc, resolved, idx);
    CHECK(resolved == sc.targetPath && idx == 0);
}

static void TestLeftoverDetection()
{
    // "Firefox (2).lnk" as UTF-16 at an odd offset inside some PIDL noise
    std::vector<unsigned char> blob = { 0x14, 0x00, 0x1f, 0x80 };
    std::wstring name = L"Firefox (2).lnk";
    blob.push_back(0x07);
    for (wchar_t c : name) { blob.push_back((unsigned char)(c & 0xff)); blob.push_back((unsigned char)(c >> 8)); }
    blob.insert(blob.end(), { 0, 0, 0x22 });
    CHECK(BlobMentionsFile(blob, L"Firefox (2).lnk"));
    CHECK(BlobMentionsFile(blob, L"FIREFOX (2).LNK"));
    CHECK(!BlobMentionsFile(blob, L"Firefox.lnk"));
    CHECK(!BlobMentionsFile({}, L"Firefox.lnk"));

    PinnedShortcut sc;
    std::wstring lnk = MakeShortcut(L"Some App (2)", nullptr, 0);
    CHECK(ReadShortcut(lnk, sc) && sc.displayName == L"Some App");
    lnk = MakeShortcut(L"Tool (beta)", nullptr, 0);
    CHECK(ReadShortcut(lnk, sc) && sc.displayName == L"Tool (beta)");
}

static void TestPackagedApps()
{
    // Taskband entries hold the app ID as UTF-16 among other bytes
    std::vector<unsigned char> blob = { 0x31, 0x00, 0x21, 0x00 }; // a stray "1!" must not match
    for (const wchar_t* s : { L"x Claude_pzs8sxrjxfjjc!Claude ", L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App" }) {
        for (const wchar_t* c = s; *c; ++c) { blob.push_back((unsigned char)(*c & 0xff)); blob.push_back((unsigned char)(*c >> 8)); }
        blob.push_back(0); blob.push_back(0);
    }
    auto ids = ExtractPinnedAppIds(blob);
    CHECK(ids.size() == 2);
    CHECK(!ids.empty() && ids[0] == L"Claude_pzs8sxrjxfjjc!Claude");
    CHECK(ids.size() > 1 && ids[1] == L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App");

    // Windows Settings is a packaged app on every Windows 10/11 install
    const std::wstring settings = L"windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel";
    CHECK(!AppDisplayName(settings).empty());
    CHECK(AppDisplayName(L"Nope_0000000000000!Nothing").empty());

    std::wstring lnk = g_tmp + L"\\Settings app.lnk";
    std::wstring ico = ExpandEnv(L"%SystemRoot%\\System32\\shell32.dll");
    CHECK(CreateAppShortcut(lnk, settings, ico, 21));
    PinnedShortcut sc;
    CHECK(ReadShortcut(lnk, sc));
    CHECK(sc.aumid == settings);            // groups with the running app on the taskbar
    CHECK(sc.iconIndex == 21 && _wcsicmp(ExpandEnv(sc.iconPath).c_str(), ico.c_str()) == 0);
    Image img;
    CHECK(LoadShellItemImage(AppsFolderPath(settings), 64, img) && HasOpaquePixel(img));
}

static void TestAdjust()
{
    Image img;
    img.w = 2; img.h = 1;
    img.rgba = { 255, 0, 0, 255,   40, 90, 200, 0 }; // red, plus a fully transparent pixel

    IconAdjust none;
    CHECK(none.IsIdentity());
    Image same = img;
    ApplyAdjust(same, none);
    CHECK(same.rgba == img.rgba);

    IconAdjust gray;
    gray.saturation = 0;
    Image g = img;
    ApplyAdjust(g, gray);
    CHECK(g.rgba[0] == g.rgba[1] && g.rgba[1] == g.rgba[2]);
    CHECK(g.rgba[3] == 255);
    CHECK(g.rgba[4] == 40 && g.rgba[7] == 0); // transparent pixels untouched

    IconAdjust hue;
    hue.hue = 120; // red -> green
    Image h = img;
    ApplyAdjust(h, hue);
    CHECK(h.rgba[1] > 240 && h.rgba[0] < 15 && h.rgba[2] < 15);

    IconAdjust bright;
    bright.brightness = 100;
    Image b = img;
    ApplyAdjust(b, bright);
    CHECK(b.rgba[0] == 255 && b.rgba[1] == 255 && b.rgba[2] == 255);

    IconAdjust tint;
    tint.tintAmount = 100;
    tint.tintColor = 0xFFFF0000; // ABGR: pure blue
    Image t = img;
    ApplyAdjust(t, tint);
    CHECK(t.rgba[2] > t.rgba[0] && t.rgba[2] > t.rgba[1]);

    CHECK(hue.Key() != gray.Key() && none.Key() == IconAdjust().Key());
}

static void TestIconLibraries()
{
    auto dash = IconIndex::ParseDashboardTree(R"({"png":["obsidian.png","firefox.png","firefox-dark.png","notes.png"],"svg":["x.svg"]})");
    CHECK(dash.size() == 4 && dash[0].name == "obsidian");
    CHECK(dash[0].Url().find("/png/obsidian.png") != std::string::npos);

    auto simple = IconIndex::ParseSimpleIcons(
        R"([{"title":"Obsidian","slug":"obsidian","hex":"7C3AED"},{"title":"Visual Studio Code","slug":"visualstudiocode","hex":"007ACC","aliases":{"aka":["VS Code"]}}])");
    CHECK(simple.size() == 2 && simple[0].brandRgb == 0x7C3AED);
    CHECK(simple[1].aliases.size() == 1);

    auto pap = IconIndex::ParseGitHubTree(
        R"({"sha":"x","tree":[{"path":"obsidian.svg","mode":"100644"},{"path":"md.obsidian.Obsidian.svg","mode":"120000"},{"path":"readme.txt"}]})", IconLibrary::Papirus);
    CHECK(pap.size() == 2);
    CHECK(IconIndex::ParseDashboardTree("not json").empty());

    IconIndex idx;
    idx.AddForTests(dash);
    idx.AddForTests(pap);
    idx.AddForTests(simple);
    auto hits = idx.Search({ "Obsidian", "Obsidian.exe" }, 10);
    CHECK(hits.size() >= 3);
    CHECK(!hits.empty() && NormalizeName(hits[0].name) == "obsidian");
    bool hasSimple = false, hasPapirusAlias = false;
    for (const auto& h : hits) {
        hasSimple |= h.lib == IconLibrary::SimpleIcons;
        hasPapirusAlias |= h.name == "md.obsidian.Obsidian";
    }
    CHECK(hasSimple && hasPapirusAlias);
    CHECK(idx.Search({ "VS Code" }, 5).size() == 1);           // via alias
    CHECK(idx.Search({ "Code" }, 5).size() >= 1);              // substring of visualstudiocode
    CHECK(idx.Search({ "zzzz-nothing" }, 5).empty());
    auto ff = idx.Search({ "firefox" }, 5);
    CHECK(ff.size() == 2 && ff[0].name == "firefox");          // plain before -dark variant

    Image svg;
    CHECK(RenderSvg(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10"><rect x="0" y="0" width="10" height="10" fill="#ff0000"/></svg>)", 32, svg));
    CHECK(svg.w == 32 && svg.rgba[(16 * 32 + 16) * 4] > 240 && svg.rgba[(16 * 32 + 16) * 4 + 3] == 255);
    CHECK(!RenderSvg("<nope>", 32, svg));

    Image tile = MakeBrandTile(R"(<svg viewBox="0 0 24 24"><path d="M4 4h16v16H4z"/></svg>)", 0x7C3AED, 64);
    CHECK(tile.w == 64 && tile.rgba[3] == 0);                                  // rounded corner is clear
    const uint8_t* mid = &tile.rgba[(32 * 64 + 32) * 4];
    CHECK(mid[0] > 240 && mid[1] > 240 && mid[2] > 240);                      // white glyph in the middle
    const uint8_t* edge = &tile.rgba[(32 * 64 + 6) * 4];
    CHECK(edge[0] == 0x7C && edge[1] == 0x3A && edge[2] == 0xED && edge[3] > 200); // brand colour on the rim
}

// Real downloads; opt-in so the default test run never needs the internet.
static void TestIconLibrariesOnline()
{
    char v[8] = {};
    if (!GetEnvironmentVariableA("ICONGER_NET_TESTS", v, sizeof(v)) || v[0] != '1') return;
    IconIndex idx;
    std::string err;
    CHECK(idx.Load(err));
    std::printf("online index: %zu icons %s\n", idx.Size(), err.c_str());
    int libs[(int)IconLibrary::Count] = {};
    std::vector<Image> sheet;
    for (const char* app : { "Obsidian", "Firefox", "Discord", "Spotify" }) {
        for (const auto& h : idx.Search({ app }, 40, 2)) {
            ++libs[(int)h.lib];
            Image img;
            std::string e;
            bool ok = FetchLibraryIcon(h, 64, img, e);
            std::printf("  %-8s %-15s %-32s %s\n", app, LibraryName(h.lib), h.name.c_str(), ok ? "ok" : e.c_str());
            CHECK(ok && img.w == 64 && HasOpaquePixel(img));
            if (ok) sheet.push_back(img);
        }
    }
    for (int l = 0; l < (int)IconLibrary::Count; ++l) CHECK(libs[l] > 0); // every library contributes

    // contact sheet for eyeballing the renders: %TEMP%\iconger-online-sheet.png
    if (!sheet.empty()) {
        const int cols = 14, cell = 72;
        const int rows = (int)(sheet.size() + cols - 1) / cols;
        std::vector<uint8_t> px((size_t)cols * cell * rows * cell * 4, 0);
        for (size_t k = 0; k < sheet.size(); ++k)
            for (int y = 0; y < 64; ++y)
                memcpy(&px[(((k / cols) * cell + 4 + y) * cols * cell + (k % cols) * cell + 4) * 4],
                       &sheet[k].rgba[(size_t)y * 64 * 4], 64 * 4);
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        stbi_write_png((std::string(tmp) + "iconger-online-sheet.png").c_str(), cols * cell, rows * cell, 4,
                       px.data(), cols * cell * 4);
    }
}

static void TestBackup()
{
    IconBackup b;
    b.Load();
    CHECK(b.Entries().empty());
    b.RecordIfMissing(L"C:\\x\\Ünïcode App.lnk", { L"%SystemRoot%\\a.dll", 7 });
    b.RecordIfMissing(L"C:\\x\\ünïcode app.lnk", { L"second.ico", 0 }); // first one wins
    b.RecordIfMissing(L"C:\\x\\NoIcon.lnk", { L"", 0 });
    CHECK(b.Save());

    IconBackup c;
    c.Load();
    CHECK(c.Entries().size() == 2);
    const IconBackupEntry* e = c.Get(L"D:\\elsewhere\\ÜNÏCODE APP.lnk");
    CHECK(e && e->iconPath == L"%SystemRoot%\\a.dll" && e->iconIndex == 7);
    e = c.Get(L"NoIcon.lnk");
    CHECK(e && e->iconPath.empty());
    c.Remove(L"NoIcon.lnk");
    CHECK(c.Save());
    IconBackup d;
    d.Load();
    CHECK(d.Entries().size() == 1 && !d.Has(L"NoIcon.lnk"));
}

int wmain()
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    g_tmp = std::wstring(tmp) + L"iconger-tests-" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(g_tmp.c_str(), nullptr);
    SetDataDirForTests(g_tmp + L"\\data");

    TestExtractSystemIcon();
    TestSquareResize();
    TestImportPngUnicodePath();
    TestShortcutIconRoundTrip();
    TestLeftoverDetection();
    TestPackagedApps();
    TestAdjust();
    TestIconLibraries();
    TestIconLibrariesOnline();
    TestBackup();

    // best-effort cleanup
    SHFILEOPSTRUCTW op = {};
    std::wstring from = g_tmp + L'\0';
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_NO_UI;
    SHFileOperationW(&op);

    CoUninitialize();
    std::printf(g_failures ? "%d check(s) failed\n" : "all tests passed\n", g_failures);
    return g_failures ? 1 : 0;
}
