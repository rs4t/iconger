#include "icon_utils.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <cstring>
#include <array>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")

#include <stb_image.h>

// ----------------------------------------------------------------------
// Internal helpers
// ----------------------------------------------------------------------

// ICO file format structures (little-endian on Windows)
#pragma pack(push, 1)
struct IcoHeader {
    WORD reserved = 0; // must be 0
    WORD type = 1;     // 1 = icon
    WORD count = 0;    // number of directory entries
};

struct IcoDirEntry {
    BYTE width = 0;
    BYTE height = 0;
    BYTE colorCount = 0;
    BYTE reserved = 0;
    WORD planes = 1;
    WORD bitsPerPixel = 32;
    DWORD imageSize = 0;
    DWORD imageOffset = 0;
};
#pragma pack(pop)

// ----------------------------------------------------------------------
// ExtractIconExW wrappers
// ----------------------------------------------------------------------

std::vector<IconInfo> ExtractIconsFromFile(const std::wstring& filePath,
                                           int maxCount) {
    std::vector<IconInfo> result;

    // First, try ExtractIconExW to get the count
    int count = (int)ExtractIconExW(filePath.c_str(), -1, nullptr, nullptr, 0);
    if (count <= 0) {
        // If that fails, maybe it's a .ico file — try loading it directly
        HICON hIcon = (HICON)LoadImageW(nullptr, filePath.c_str(),
                                         IMAGE_ICON, 32, 32,
                                         LR_LOADFROMFILE);
        if (hIcon) {
            IconInfo info;
            info.sourcePath = filePath;
            info.index = 0;
            info.hIcon = hIcon;
            result.push_back(info);
        }
        return result;
    }

    if (count > maxCount) count = maxCount;

    // Allocate arrays
    std::vector<HICON> largeIcons(count, nullptr);
    std::vector<HICON> smallIcons(count, nullptr);

    int extracted = (int)ExtractIconExW(filePath.c_str(), 0,
                                         largeIcons.data(),
                                         smallIcons.data(),
                                         count);
    if (extracted <= 0) {
        // Fall back to legacy ExtractIcon
        HICON hIcon = ExtractIconW(GetModuleHandleW(nullptr), filePath.c_str(), 0);
        if (hIcon && hIcon != (HICON)(-1)) {
            IconInfo info;
            info.sourcePath = filePath;
            info.index = 0;
            info.hIcon = hIcon; // caller's responsibility to DestroyIcon
            result.push_back(info);
        }
        return result;
    }

    for (int i = 0; i < extracted; ++i) {
        IconInfo info;
        info.sourcePath = filePath;
        info.index = i;
        // Prefer large (32x32) icon; fall back to small
        if (largeIcons[i]) {
            info.hIcon = largeIcons[i];
        } else {
            info.hIcon = smallIcons[i];
        }
        if (info.hIcon) {
            result.push_back(info);
        }
    }

    return result;
}

HICON ExtractSingleIcon(const std::wstring& filePath, int index) {
    // Try ExtractIconExW first
    HICON large = nullptr;
    HICON hSmall = nullptr;
    int extracted = (int)ExtractIconExW(filePath.c_str(), index,
                                         &large, &hSmall, 1);
    if (extracted > 0) {
        if (large) return large;
        if (hSmall) return hSmall;
    }

    // Legacy fallback
    HICON hIcon = ExtractIconW(GetModuleHandleW(nullptr), filePath.c_str(), index);
    if (hIcon && hIcon != (HICON)(-1))
        return hIcon;

    // Last resort: try loading as image
    return (HICON)LoadImageW(nullptr, filePath.c_str(),
                              IMAGE_ICON, 32, 32,
                              LR_LOADFROMFILE);
}

void FreeIcon(HICON& hIcon) {
    if (hIcon) {
        DestroyIcon(hIcon);
        hIcon = nullptr;
    }
}

// ----------------------------------------------------------------------
// DX11 texture helpers
// ----------------------------------------------------------------------

ID3D11ShaderResourceView* CreateTextureFromHICON(
    ID3D11Device* device,
    HICON hIcon)
{
    if (!device || !hIcon) return nullptr;

    // Get icon info
    ICONINFO iconInfo = {};
    if (!GetIconInfo(hIcon, &iconInfo)) return nullptr;

    // Get icon dimensions from the bitmap
    BITMAP bm = {};
    if (iconInfo.hbmColor) {
        GetObject(iconInfo.hbmColor, sizeof(bm), &bm);
    } else if (iconInfo.hbmMask) {
        GetObject(iconInfo.hbmMask, sizeof(bm), &bm);
    }

    int w = bm.bmWidth;
    int h = bm.bmHeight;

    // Clean up bitmaps
    if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
    if (iconInfo.hbmMask)  DeleteObject(iconInfo.hbmMask);

    if (w <= 0 || h <= 0) return nullptr;

    // Create a DC and select the icon
    HDC hdc = CreateCompatibleDC(nullptr);
    if (!hdc) return nullptr;

    // Use DrawIconEx to get 32bpp ARGB
    HBITMAP hBitmap = CreateCompatibleBitmap(hdc, w, h);
    if (!hBitmap) {
        DeleteDC(hdc);
        return nullptr;
    }

    HGDIOBJ old = SelectObject(hdc, hBitmap);

    // Draw the icon onto the bitmap
    DrawIconEx(hdc, 0, 0, hIcon, w, h, 0, nullptr, DI_NORMAL);

    // Get the bitmap bits (32bpp BGRA)
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    std::vector<uint8_t> pixels(w * h * 4);
    int got = GetDIBits(hdc, hBitmap, 0, h, pixels.data(), &bi, DIB_RGB_COLORS);

    SelectObject(hdc, old);
    DeleteObject(hBitmap);
    DeleteDC(hdc);

    if (!got) return nullptr;

    // Convert BGRA -> RGBA for DX11
    // Actually DX11 expects RGBA if using DXGI_FORMAT_R8G8B8A8_UNORM.
    // But with CreateTexture2D, BGRA is also valid with DXGI_FORMAT_B8G8R8A8_UNORM.
    // We'll use B8G8R8A8_UNORM since the data is already BGRA.
    // Just swap if needed... Actually B8G8R8A8 is what GDI gives us natively.
    // DXGI_FORMAT_B8G8R8A8_UNORM is correct for this data.

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = pixels.data();
    initData.SysMemPitch = w * 4;

    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, &initData, &tex);
    if (FAILED(hr) || !tex) return nullptr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* srv = nullptr;
    hr = device->CreateShaderResourceView(tex, &srvDesc, &srv);
    tex->Release();

    if (FAILED(hr)) return nullptr;
    return srv;
}

ID3D11ShaderResourceView* CreateTextureFromRGBA(
    ID3D11Device* device,
    const uint8_t* rgbaData,
    int width,
    int height)
{
    if (!device || !rgbaData || width <= 0 || height <= 0)
        return nullptr;

    // We need to convert RGBA -> BGRA for DXGI_FORMAT_B8G8R8A8_UNORM
    // Or use DXGI_FORMAT_R8G8B8A8_UNORM if we keep it as RGBA.
    // Let's use R8G8B8A8_UNORM since that's RGBA natively.

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = rgbaData;
    initData.SysMemPitch = width * 4;

    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, &initData, &tex);
    if (FAILED(hr) || !tex) return nullptr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* srv = nullptr;
    HRESULT hr2 = device->CreateShaderResourceView(tex, &srvDesc, &srv);
    tex->Release();
    if (FAILED(hr2)) return nullptr;
    return srv;
}

// ----------------------------------------------------------------------
// PNG → ICO conversion
// ----------------------------------------------------------------------

std::wstring ConvertPNGToICO(const std::wstring& pngFilePath) {
    // Decode PNG to RGBA using stb_image
    int w = 0, h = 0, channels = 0;
    stbi_uc* data = stbi_load(
        std::filesystem::path(pngFilePath).string().c_str(),
        &w, &h, &channels, 4); // force RGBA
    if (!data) return L"";

    // Create a temp file path
    wchar_t tempPath[MAX_PATH] = {};
    wchar_t tempFile[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, tempPath)) {
        stbi_image_free(data);
        return L"";
    }
    if (!GetTempFileNameW(tempPath, L"ico", 0, tempFile)) {
        stbi_image_free(data);
        return L"";
    }

    // GetTempFileNameW always creates a .tmp file on disk.
    // Delete it, then replace the extension with .ico.
    std::wstring icoPath = tempFile;
    DeleteFileW(icoPath.c_str());

    // Replace .tmp extension with .ico
    size_t dot = icoPath.rfind(L'.');
    if (dot != std::wstring::npos) {
        icoPath = icoPath.substr(0, dot) + L".ico";
    } else {
        icoPath += L".ico";
    }

    bool ok = WriteICOFromRGBA(icoPath, data, w, h);

    stbi_image_free(data);

    if (!ok) {
#ifdef _DEBUG
        OutputDebugStringW(L"Iconger: ConvertPNGToICO - WriteICOFromRGBA failed\n");
#endif
        DeleteFileW(icoPath.c_str());
        return L"";
    }

#ifdef _DEBUG
    std::array<wchar_t, 256> dbg{};
    swprintf_s(dbg.data(), dbg.size(),
        L"Iconger: ConvertPNGToICO -> %dx%d -> %S, ok=%d\n",
        w, h, std::filesystem::path(icoPath).string().c_str(), (int)ok);
    OutputDebugStringW(dbg.data());
#endif

    return icoPath;
}

bool WriteICOFromRGBA(const std::wstring& icoFilePath,
                      const uint8_t* rgbaData,
                      int width,
                      int height)
{
    if (width <= 0 || height <= 0 || !rgbaData) return false;

    // ICO stores a BMP DIB inside the image data.
    // The DIB must start with a BITMAPINFOHEADER (40 bytes):
    //   biSize, width, height (doubled for ICO: AND+OR mask),
    //   planes, bitCount, compression=BI_RGB, imageSize,
    //   xPelsPerMeter, yPelsPerMeter, clrUsed, clrImportant
    // Then the XOR mask (BGRA pixels, bottom-up), row-padded to 4 bytes.
    // Then the AND mask (1 bit per pixel, bottom-up), row-padded to 4 bytes.

    // For ICO, the height in the BITMAPINFOHEADER is doubled (AND+OR).
    // XOR mask: 32 bpp, width*4 bytes per row (already DWORD-aligned)
    // AND mask: 1 bpp, ((width + 31) / 32) * 4 bytes per row
    int xorRowBytes = width * 4;  // naturally DWORD-aligned for 32bpp
    int andRowBytes = ((width + 31) / 32) * 4;

    int xorSize = xorRowBytes * height;
    int andSize = andRowBytes * height;

    BITMAPINFOHEADER bih = {};
    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = width;
    bih.biHeight = height * 2;  // height doubled for ICO (XOR + AND)
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;
    bih.biSizeImage = xorSize + andSize;

    // Build the AND mask — 1-bit transparency mask based on alpha channel
    std::vector<uint8_t> andMask(andSize, 0);
    for (int y = 0; y < height; ++y) {
        int srcY = (height - 1 - y); // AND mask is bottom-up too
        for (int x = 0; x < width; ++x) {
            int srcIdx = (srcY * width + x) * 4;
            uint8_t a = rgbaData[srcIdx + 3];
            // Set AND bit for transparent pixels (alpha < 128)
            if (a < 128) {
                int andByte = y * andRowBytes + x / 8;
                int andBit = 7 - (x % 8);
                andMask[andByte] |= (1 << andBit);
            }
        }
    }

    // Build the XOR mask (BGRA pixels, bottom-up)
    std::vector<uint8_t> xorMask(xorSize, 0);
    for (int y = 0; y < height; ++y) {
        int srcY = (height - 1 - y); // BMP stores rows bottom-to-top
        for (int x = 0; x < width; ++x) {
            int srcIdx = (srcY * width + x) * 4;
            int dstIdx = y * xorRowBytes + x * 4;
            xorMask[dstIdx + 0] = rgbaData[srcIdx + 2]; // B
            xorMask[dstIdx + 1] = rgbaData[srcIdx + 1]; // G
            xorMask[dstIdx + 2] = rgbaData[srcIdx + 0]; // R
            xorMask[dstIdx + 3] = rgbaData[srcIdx + 3]; // A
        }
    }

    // ICO header
    IcoHeader header = {};
    header.type = 1; // ICO
    header.count = 1;

    // ICO directory entry
    IcoDirEntry entry = {};
    entry.width = (BYTE)(width >= 256 ? 0 : width);
    entry.height = (BYTE)(height >= 256 ? 0 : height);
    entry.planes = 1;
    entry.bitsPerPixel = 32;
    entry.imageSize = sizeof(BITMAPINFOHEADER) + xorSize + andSize;
    entry.imageOffset = sizeof(IcoHeader) + sizeof(IcoDirEntry);

    // Write the file
    std::ofstream ofs(std::filesystem::path(icoFilePath), std::ios::binary);
    if (!ofs) return false;

    ofs.write(reinterpret_cast<const char*>(&header), sizeof(header));
    ofs.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
    ofs.write(reinterpret_cast<const char*>(&bih), sizeof(bih));
    ofs.write(reinterpret_cast<const char*>(xorMask.data()), xorSize);
    ofs.write(reinterpret_cast<const char*>(andMask.data()), andSize);

    bool ok = ofs.good();
    ofs.close();

    // Debug log via OutputDebugStringW
#ifdef _DEBUG
    std::array<wchar_t, 256> dbg{};
    swprintf_s(dbg.data(), dbg.size(),
        L"Iconger: WriteICOFromRGBA -> %dx%d, wrote %zu bytes, ok=%d\n",
        width, height,
        (size_t)(sizeof(header) + sizeof(entry) + sizeof(bih) + xorSize + andSize),
        (int)ok);
    OutputDebugStringW(dbg.data());
#endif

    return ok;
}

// (file end)