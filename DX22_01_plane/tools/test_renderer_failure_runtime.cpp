#include "Application.h"
#include "Renderer.h"
#include <cassert>
#include <iostream>
#include <string_view>
#include <wrl/client.h>
#include "test_checks.h"

// A missing window deterministically fails the real swap-chain creation path.
HWND Application::m_hWnd{};
uint32_t Application::m_Width = 1280;
uint32_t Application::m_Height = 720;
Application::Application(uint32_t width, uint32_t height)
{
    m_Width = width;
    m_Height = height;
    m_hWnd = CreateWindowExW(0, L"STATIC", L"Renderer resize regression",
        WS_OVERLAPPED, 0, 0, width, height, nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
}
Application::~Application()
{
    if (m_hWnd) DestroyWindow(m_hWnd);
    m_hWnd = nullptr;
}

static void CheckTargets(UINT width, UINT height)
{
    using Microsoft::WRL::ComPtr;
    Renderer::DrawStart();
    auto* context = Renderer::GetDeviceContext();
    ComPtr<ID3D11RenderTargetView> color;
    ComPtr<ID3D11DepthStencilView> depth;
    context->OMGetRenderTargets(1, color.GetAddressOf(), depth.GetAddressOf());
    assert(color && depth);
    ComPtr<ID3D11Resource> colorResource, depthResource;
    color->GetResource(colorResource.GetAddressOf());
    depth->GetResource(depthResource.GetAddressOf());
    ComPtr<ID3D11Texture2D> colorTexture, depthTexture;
    assert(SUCCEEDED(colorResource.As(&colorTexture)));
    assert(SUCCEEDED(depthResource.As(&depthTexture)));
    D3D11_TEXTURE2D_DESC colorDesc{}, depthDesc{};
    colorTexture->GetDesc(&colorDesc);
    depthTexture->GetDesc(&depthDesc);
    assert(colorDesc.Width == width && colorDesc.Height == height);
    assert(depthDesc.Width == width && depthDesc.Height == height);
    assert(colorDesc.SampleDesc.Count == depthDesc.SampleDesc.Count);
    assert(colorDesc.SampleDesc.Quality == depthDesc.SampleDesc.Quality);
}

int main(int argc, char** argv)
{
    Renderer::Uninit();
    assert(FAILED(Renderer::Init()));
    Renderer::Uninit();
    assert(Renderer::GetDevice() == nullptr);
    assert(Renderer::GetDeviceContext() == nullptr);
    Renderer::Uninit();
    {
        Application window(1280, 720);
        assert(Application::GetWindow() != nullptr);
        assert(SUCCEEDED(Renderer::Init()));
        // Settings resize before the first DrawStart; later resizes happen
        // with the old target still bound to the immediate context.
        if (argc == 2 && std::string_view(argv[1]) == "--bound-first")
            CheckTargets(1280, 720);
        assert(SUCCEEDED(Renderer::ResizeWindow(1540, 845)));
        CheckTargets(1540, 845);
        assert(SUCCEEDED(Renderer::ResizeWindow(960, 540)));
        CheckTargets(960, 540);
        {
            // An external owner keeps the old backbuffer alive. DXGI must
            // reject resizing, and the renderer must report that failure
            // while retaining a usable render/depth pair at the old size.
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> retained;
            Renderer::GetDeviceContext()->OMGetRenderTargets(
                1, retained.GetAddressOf(), nullptr);
            assert(retained);
            assert(FAILED(Renderer::ResizeWindow(640, 360)));
            CheckTargets(960, 540);
        }
        assert(Renderer::ResizeWindow(0, 540) == E_INVALIDARG);
        assert(Renderer::ResizeWindow(960, 0) == E_INVALIDARG);
        assert(Renderer::ResizeWindow(-1, 540) == E_INVALIDARG);
        CheckTargets(960, 540);
        assert(SUCCEEDED(Renderer::ResizeWindow(640, 360)));
        CheckTargets(640, 360);
        Renderer::Uninit();
    }
    std::cout << "Renderer init failure, hidden-window resize, color/depth dimensions, binding, invalid size, and cleanup passed.\n";
}
