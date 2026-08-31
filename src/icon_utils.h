#pragma once
#include <windows.h>
#include <d3d11.h>
#include <string>
#include <vector>

struct IconInfo {
    std::wstring sourcePath;   // The .exe/.dll/.ico file path
    int          index = 0;    // Icon resource index
    HICON        hIcon = nullptr;
};

// ---------- Win32 icon extraction helpers ----------

/// Extract icons from a file (.exe, .dll, .ico) using ExtractIconExW.
/// Returns a vector of all available icons (up to maxCount).
std::vector<IconInfo> ExtractIconsFromFile(const std::wstring& filePath,
                                           int maxCount = 100);

/// Extract a single icon at a given index from a file.
HICON ExtractSingleIcon(const std::wstring& filePath, int index);

/// Destroy an HICON and null out the handle.
void FreeIcon(HICON& hIcon);

// ---------- DX11 texture helpers ----------

/// Create an ID3D11ShaderResourceView from an HICON so ImGui can render it.
/// Returns nullptr on failure. The caller owns the resource (must Release).
ID3D11ShaderResourceView* CreateTextureFromHICON(
    ID3D11Device* device,
    HICON hIcon);

/// Create a DX11 texture from raw RGBA pixel data (common for stb_image output).
/// The caller owns the returned SRV.
ID3D11ShaderResourceView* CreateTextureFromRGBA(
    ID3D11Device* device,
    const uint8_t* rgbaData,
    int width,
    int height);

// ---------- PNG → ICO conversion (stb_image) ----------

/// Convert a PNG file (or any file stb_image can decode) to a temporary .ico file.
/// Returns the path to the generated .ico file, or empty on failure.
/// The caller should delete the temp file when done.
std::wstring ConvertPNGToICO(const std::wstring& pngFilePath);

/// Convert raw RGBA data to a .ico file (written to tempPath).
/// Returns true on success. The .ico will contain a single 32x32 entry.
bool WriteICOFromRGBA(const std::wstring& icoFilePath,
                      const uint8_t* rgbaData,
                      int width,
                      int height);