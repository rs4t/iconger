// Iconger core tests. Plain asserts, no framework: returns non-zero on failure.
// Everything runs in a temp folder; the real pinned folder is never touched.
#include "app_paths.h"
#include "icon_backup.h"
#include "icon_utils.h"
#include "shell_link.h"
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <cstdio>
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
