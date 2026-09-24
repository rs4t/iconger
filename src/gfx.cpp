#include "gfx.h"
#include "icon_utils.h"
#include <backends/imgui_impl_dx11.h>
#include <iterator>
#include <vector>

static ID3D11Device*           g_device = nullptr;
static ID3D11DeviceContext*    g_context = nullptr;
static IDXGISwapChain*         g_swapChain = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static bool                    g_occluded = false;

// Textures dropped while a frame is being built may already be referenced by that
// frame's draw list (e.g. the icon preview is drawn, then a slider replaces it).
// Releasing them right away left the renderer using a freed view: heap corruption.
// So they are parked here and released once the frame has been rendered.
static std::vector<ID3D11ShaderResourceView*> g_releaseAfterFrame;

static void ReleaseParkedTextures()
{
    for (auto* srv : g_releaseAfterFrame) srv->Release();
    g_releaseAfterFrame.clear();
}

ID3D11Device* GfxDevice() { return g_device; }
ID3D11DeviceContext* GfxContext() { return g_context; }

static void CreateRenderTarget()
{
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back)))) {
        g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
    }
}

static void ReleaseRenderTarget()
{
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

bool GfxInit(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        levels, (UINT)std::size(levels), D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device, &got, &g_context);
    if (hr == DXGI_ERROR_UNSUPPORTED) // no usable GPU (VMs, remote sessions): software rasterizer
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
            levels, (UINT)std::size(levels), D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device, &got, &g_context);
    if (FAILED(hr)) return false;

    // we handle fullscreen ourselves (i.e. not at all): no Alt+Enter mode switch
    IDXGIFactory* factory = nullptr;
    if (SUCCEEDED(g_swapChain->GetParent(IID_PPV_ARGS(&factory)))) {
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        factory->Release();
    }
    CreateRenderTarget();
    return true;
}

void GfxShutdown()
{
    ReleaseParkedTextures();
    ReleaseRenderTarget();
    if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}

void GfxResize(UINT width, UINT height)
{
    if (!g_swapChain || !width || !height) return;
    ReleaseRenderTarget();
    g_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    CreateRenderTarget();
}

void GfxRender(const float clearColor[4])
{
    g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_context->ClearRenderTargetView(g_rtv, clearColor);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    ReleaseParkedTextures(); // the frame that could still reference them is submitted
}

bool GfxPresent()
{
    // While occluded, test with DXGI_PRESENT_TEST instead of rendering: the old
    // loop spun at 100% CPU when minimized because Present returned instantly.
    if (g_occluded && g_swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        return false;
    g_occluded = g_swapChain->Present(1, 0) == DXGI_STATUS_OCCLUDED;
    return !g_occluded;
}

Texture::Texture(const Image& img)
{
    if (!g_device || img.empty()) return;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = (UINT)img.w;
    desc.Height = (UINT)img.h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init = { img.rgba.data(), (UINT)img.w * 4, 0 };
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(g_device->CreateTexture2D(&desc, &init, &tex))) return;
    g_device->CreateShaderResourceView(tex, nullptr, &m_srv);
    tex->Release();
}

Texture& Texture::operator=(Texture&& o) noexcept
{
    if (this != &o) {
        Reset();
        m_srv = o.m_srv;
        o.m_srv = nullptr;
    }
    return *this;
}

void Texture::Reset()
{
    if (!m_srv) return;
    if (g_device) g_releaseAfterFrame.push_back(m_srv);
    else m_srv->Release(); // after GfxShutdown there are no more frames to wait for
    m_srv = nullptr;
}
