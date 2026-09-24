#include "icon_utils.h"
#include "app_paths.h"
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>

#include <stb_image.h>
#include <stb_image_resize2.h>
#include <stb_image_write.h>

using Microsoft::WRL::ComPtr;

// ----------------------------------------------------------------------
// HICON / HBITMAP -> RGBA
// ----------------------------------------------------------------------

static bool ReadBitmap32(HBITMAP bmp, int w, int h, std::vector<uint8_t>& bgra)
{
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bgra.assign((size_t)w * h * 4, 0);
    HDC dc = GetDC(nullptr);
    int got = GetDIBits(dc, bmp, 0, h, bgra.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    return got == h;
}

// The old version drew the icon into CreateCompatibleBitmap(memoryDC), which is a
// 1-bit monochrome bitmap, so every pixel came back with alpha 0 (invisible).
// Read the icon's own colour + mask bitmaps instead.
static bool ImageFromHICON(HICON icon, Image& out)
{
    ICONINFO ii = {};
    if (!GetIconInfo(icon, &ii)) return false;

    bool ok = false;
    BITMAP bm = {};
    if (ii.hbmColor && GetObjectW(ii.hbmColor, sizeof(bm), &bm)) {
        int w = bm.bmWidth, h = bm.bmHeight;
        std::vector<uint8_t> bgra;
        if (w > 0 && h > 0 && ReadBitmap32(ii.hbmColor, w, h, bgra)) {
            bool hasAlpha = false;
            for (size_t i = 3; i < bgra.size() && !hasAlpha; i += 4) hasAlpha = bgra[i] != 0;

            // Old-style icons without an alpha channel: transparency lives in the mask.
            std::vector<uint8_t> mask;
            if (!hasAlpha && ii.hbmMask) ReadBitmap32(ii.hbmMask, w, h, mask);

            out.w = w; out.h = h;
            out.rgba.resize(bgra.size());
            for (size_t i = 0; i < bgra.size(); i += 4) {
                out.rgba[i + 0] = bgra[i + 2];
                out.rgba[i + 1] = bgra[i + 1];
                out.rgba[i + 2] = bgra[i + 0];
                out.rgba[i + 3] = hasAlpha ? bgra[i + 3]
                                : (!mask.empty() && mask[i]) ? 0 : 255;
            }
            ok = true;
        }
    }
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    return ok;
}

int CountIcons(const std::wstring& path)
{
    return (int)ExtractIconExW(path.c_str(), -1, nullptr, nullptr, 0);
}

bool LoadIconImage(const std::wstring& path, int index, int size, Image& out)
{
    if (path.empty()) return false;
    HICON icon = nullptr;
    // SHDefExtractIcon picks the closest size from the icon group and scales it,
    // unlike ExtractIconEx which is stuck at the 32 px system metric.
    // Only the large handle is requested, so nothing leaks (the old code leaked every
    // small icon ExtractIconEx returned).
    if (SHDefExtractIconW(path.c_str(), index, 0, &icon, nullptr, MAKELONG(size, size)) != S_OK || !icon) {
        UINT id = 0;
        if (PrivateExtractIconsW(path.c_str(), index, size, size, &icon, &id, 1, 0) != 1)
            icon = nullptr;
    }
    if (!icon) return false;
    bool ok = ImageFromHICON(icon, out);
    DestroyIcon(icon);
    return ok;
}

bool LoadShellItemImage(const std::wstring& path, int size, Image& out)
{
    ComPtr<IShellItemImageFactory> factory;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&factory))))
        return false;
    HBITMAP bmp = nullptr;
    if (FAILED(factory->GetImage({ size, size }, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bmp)) || !bmp)
        return false;

    bool ok = false;
    BITMAP bm = {};
    std::vector<uint8_t> bgra;
    if (GetObjectW(bmp, sizeof(bm), &bm) && ReadBitmap32(bmp, bm.bmWidth, bm.bmHeight, bgra)) {
        out.w = bm.bmWidth; out.h = bm.bmHeight;
        out.rgba.resize(bgra.size());
        for (size_t i = 0; i < bgra.size(); i += 4) {
            uint8_t a = bgra[i + 3];
            auto unpremul = [a](uint8_t c) { return a ? (uint8_t)std::min(255, c * 255 / a) : (uint8_t)0; };
            out.rgba[i + 0] = unpremul(bgra[i + 2]);
            out.rgba[i + 1] = unpremul(bgra[i + 1]);
            out.rgba[i + 2] = unpremul(bgra[i + 0]);
            out.rgba[i + 3] = a;
        }
        ok = true;
    }
    DeleteObject(bmp);
    return ok;
}

// ----------------------------------------------------------------------
// Import
// ----------------------------------------------------------------------

IconSourceKind ClassifyIconSource(const std::wstring& path)
{
    std::wstring ext = LowerExt(path);
    if (ext == L".ico") return IconSourceKind::IcoFile;
    if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".bmp" ||
        ext == L".gif" || ext == L".tga" || ext == L".psd")
        return IconSourceKind::ImageFile;
    if (ext == L".exe" || ext == L".dll" || ext == L".icl" || ext == L".cpl" ||
        ext == L".mun" || ext == L".scr" || ext == L".ocx")
        return IconSourceKind::IconLibrary;
    return IconSourceKind::Unsupported;
}

static std::wstring HashName(const std::vector<uint8_t>& bytes)
{
    uint64_t h = 1469598103934665603ull; // FNV-1a
    for (uint8_t b : bytes) { h ^= b; h *= 1099511628211ull; }
    wchar_t buf[17];
    swprintf_s(buf, L"%016llx", (unsigned long long)h);
    return buf;
}

static std::wstring SafeStem(const std::wstring& path)
{
    std::wstring stem = FileStem(path);
    for (auto& c : stem)
        if (wcschr(L"<>:\"/\\|?*", c) || c < 32) c = L'_';
    if (stem.size() > 40) stem.resize(40);
    return stem.empty() ? L"icon" : stem;
}

Image MakeSquareResized(const Image& src, int size)
{
    int side = std::max(src.w, src.h);
    Image square;
    square.w = square.h = side;
    square.rgba.assign((size_t)side * side * 4, 0);
    int ox = (side - src.w) / 2, oy = (side - src.h) / 2;
    for (int y = 0; y < src.h; ++y)
        memcpy(&square.rgba[((size_t)(y + oy) * side + ox) * 4], &src.rgba[(size_t)y * src.w * 4], (size_t)src.w * 4);

    if (side == size) return square;
    Image out;
    out.w = out.h = size;
    out.rgba.resize((size_t)size * size * 4);
    // _srgb + STBIR_RGBA = gamma-correct, alpha-weighted filtering (no dark fringes)
    stbir_resize_uint8_srgb(square.rgba.data(), side, side, 0, out.rgba.data(), size, size, 0, STBIR_RGBA);
    return out;
}

std::wstring ImportIconFile(const std::wstring& src, std::string& error)
{
    std::vector<uint8_t> bytes;
    if (!ReadWholeFile(src, bytes) || bytes.empty()) { error = "Could not read the file."; return {}; }

    std::wstring dst = IconsDir() + L"\\" + SafeStem(src) + L"-" + HashName(bytes).substr(0, 8) + L".ico";
    if (FileExists(dst)) return dst; // same content imported before

    switch (ClassifyIconSource(src)) {
    case IconSourceKind::IcoFile: {
        // Keep our own copy so deleting the download doesn't blank the taskbar icon.
        HICON test = (HICON)LoadImageW(nullptr, src.c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE);
        if (!test) { error = "That .ico file looks broken."; return {}; }
        DestroyIcon(test);
        if (!CopyFileW(src.c_str(), dst.c_str(), FALSE)) { error = "Could not copy the icon."; return {}; }
        return dst;
    }
    case IconSourceKind::ImageFile: {
        // Decode from memory: stbi_load(path) choked on non-ASCII paths.
        int w = 0, h = 0, n = 0;
        stbi_uc* px = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &n, 4);
        if (!px) { error = std::string("Could not decode image: ") + stbi_failure_reason(); return {}; }
        if (std::max(w, h) > 8192) { stbi_image_free(px); error = "Image is too large (max 8192 px)."; return {}; }
        Image img;
        img.w = w; img.h = h;
        img.rgba.assign(px, px + (size_t)w * h * 4);
        stbi_image_free(px);

        std::vector<Image> sizes;
        for (int s : { 256, 128, 64, 48, 32, 24, 16 })
            sizes.push_back(MakeSquareResized(img, s));
        if (!WriteIco(dst, sizes)) { error = "Could not write the .ico file."; return {}; }
        return dst;
    }
    default:
        error = "Unsupported file type.";
        return {};
    }
}

// ----------------------------------------------------------------------
// ICO writer
// ----------------------------------------------------------------------

#pragma pack(push, 1)
struct IcoHeader { WORD reserved = 0, type = 1, count = 0; };
struct IcoDirEntry {
    BYTE width = 0, height = 0, colorCount = 0, reserved = 0;
    WORD planes = 1, bitCount = 32;
    DWORD bytes = 0, offset = 0;
};
#pragma pack(pop)

static std::vector<uint8_t> EncodeBmpEntry(const Image& img)
{
    const int w = img.w, h = img.h;
    const int andRow = ((w + 31) / 32) * 4;
    BITMAPINFOHEADER bih = {};
    bih.biSize = sizeof(bih);
    bih.biWidth = w;
    bih.biHeight = h * 2; // colour + mask
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;
    bih.biSizeImage = (DWORD)(w * 4 * h + andRow * h);

    std::vector<uint8_t> out(sizeof(bih) + bih.biSizeImage, 0);
    memcpy(out.data(), &bih, sizeof(bih));
    uint8_t* xorBits = out.data() + sizeof(bih);
    uint8_t* andBits = xorBits + (size_t)w * 4 * h;
    for (int y = 0; y < h; ++y) {
        const uint8_t* srcRow = &img.rgba[(size_t)(h - 1 - y) * w * 4]; // bottom-up
        for (int x = 0; x < w; ++x) {
            const uint8_t* p = srcRow + x * 4;
            uint8_t* d = xorBits + ((size_t)y * w + x) * 4;
            d[0] = p[2]; d[1] = p[1]; d[2] = p[0]; d[3] = p[3];
            if (p[3] < 128) andBits[y * andRow + x / 8] |= (uint8_t)(0x80 >> (x % 8));
        }
    }
    return out;
}

static std::vector<uint8_t> EncodePngEntry(const Image& img)
{
    std::vector<uint8_t> out;
    stbi_write_png_to_func([](void* ctx, void* data, int size) {
        auto* v = static_cast<std::vector<uint8_t>*>(ctx);
        v->insert(v->end(), (uint8_t*)data, (uint8_t*)data + size);
    }, &out, img.w, img.h, 4, img.rgba.data(), img.w * 4);
    return out;
}

bool WriteIco(const std::wstring& path, const std::vector<Image>& images)
{
    if (images.empty()) return false;
    std::vector<std::vector<uint8_t>> blobs;
    for (const Image& img : images) {
        if (img.empty() || img.w > 256 || img.h > 256) return false;
        // 256 px as PNG keeps the file small; smaller sizes as BMP for old consumers.
        blobs.push_back(img.w == 256 ? EncodePngEntry(img) : EncodeBmpEntry(img));
        if (blobs.back().empty()) return false;
    }

    IcoHeader header;
    header.count = (WORD)images.size();
    std::vector<uint8_t> file(sizeof(header) + sizeof(IcoDirEntry) * images.size());
    memcpy(file.data(), &header, sizeof(header));
    DWORD offset = (DWORD)file.size();
    for (size_t i = 0; i < images.size(); ++i) {
        IcoDirEntry e;
        e.width = (BYTE)(images[i].w == 256 ? 0 : images[i].w);
        e.height = (BYTE)(images[i].h == 256 ? 0 : images[i].h);
        e.bytes = (DWORD)blobs[i].size();
        e.offset = offset;
        offset += e.bytes;
        memcpy(file.data() + sizeof(header) + i * sizeof(e), &e, sizeof(e));
    }
    for (auto& b : blobs) file.insert(file.end(), b.begin(), b.end());

    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(h, file.data(), (DWORD)file.size(), &written, nullptr) && written == file.size();
    CloseHandle(h);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}
