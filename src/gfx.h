#pragma once
#include <windows.h>
#include <d3d11.h>
#include <imgui.h>

struct Image;

// D3D11 device + swap chain for the main window.
bool GfxInit(HWND hwnd);
void GfxShutdown();
void GfxResize(UINT width, UINT height);
void GfxRender(const float clearColor[4]);
/// Present; returns false when the window is occluded/minimized (caller should idle).
bool GfxPresent();
ID3D11Device* GfxDevice();
ID3D11DeviceContext* GfxContext();

/// GPU copy of an Image. Move-only, releases itself.
class Texture {
public:
    Texture() = default;
    explicit Texture(const Image& img);
    ~Texture() { Reset(); }
    Texture(Texture&& o) noexcept : m_srv(o.m_srv) { o.m_srv = nullptr; }
    Texture& operator=(Texture&& o) noexcept;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    void Reset();
    explicit operator bool() const { return m_srv != nullptr; }
    ImTextureID Id() const { return (ImTextureID)(intptr_t)m_srv; }

private:
    ID3D11ShaderResourceView* m_srv = nullptr;
};
