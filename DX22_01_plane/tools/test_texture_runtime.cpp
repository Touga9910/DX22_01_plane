#include "Renderer.h"
#include "Texture.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>

// Give the production Texture implementation a window-independent D3D device.
ID3D11Device* Renderer::m_pDevice{};
ID3D11DeviceContext* Renderer::m_pDeviceContext{};
HRESULT Renderer::Init()
{
    return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &m_pDevice, nullptr, &m_pDeviceContext);
}
void Renderer::Uninit()
{
    if (m_pDeviceContext) m_pDeviceContext->ClearState();
    SAFE_RELEASE(m_pDeviceContext);
    SAFE_RELEASE(m_pDevice);
}

static std::vector<unsigned char> Image(unsigned char width)
{
    std::vector<unsigned char> data(18 + width * 4, 255);
    std::fill(data.begin(), data.begin() + 18, 0);
    data[2] = 2; // Uncompressed true-color TGA.
    data[12] = width;
    data[14] = 1;
    data[16] = 32;
    data[17] = 8;
    return data;
}
static ComPtr<ID3D11ShaderResourceView> BoundView(Texture& texture, UINT width)
{
    texture.SetGPU();
    ComPtr<ID3D11ShaderResourceView> view;
    Renderer::GetDeviceContext()->PSGetShaderResources(0, 1, view.GetAddressOf());
    assert(view);
    ComPtr<ID3D11Resource> resource;
    view->GetResource(resource.GetAddressOf());
    ComPtr<ID3D11Texture2D> image;
    assert(SUCCEEDED(resource.As(&image)));
    D3D11_TEXTURE2D_DESC desc{};
    image->GetDesc(&desc);
    assert(desc.Width == width && desc.Height == 1);
    return view;
}
int main()
{
    const auto first = Image(1), second = Image(2);
    const unsigned char corrupt[]{1, 2, 3};
    Texture texture;
    assert(!texture.LoadFromFemory(nullptr, 3));
    assert(!texture.LoadFromFemory(corrupt, -1));
    assert(!texture.LoadFromFemory(corrupt, sizeof(corrupt)));
    assert(!texture.LoadFromFemory(first.data(), static_cast<int>(first.size())));
    texture.SetGPU(); // Missing renderer is safe.
    assert(SUCCEEDED(Renderer::Init()));
    {
        assert(texture.LoadFromFemory(first.data(), static_cast<int>(first.size())));
        auto oldView = BoundView(texture, 1);
        assert(!texture.LoadFromFemory(corrupt, sizeof(corrupt)));
        assert(!texture.Load("missing-texture-fixture.tga"));
        assert(BoundView(texture, 1).Get() == oldView.Get());
        std::ofstream file("texture-fixture.tga", std::ios::binary);
        file.write(reinterpret_cast<const char*>(second.data()), second.size());
        file.close();
        assert(texture.Load("texture-fixture.tga"));
        auto newView = BoundView(texture, 2);
        assert(newView.Get() != oldView.Get());
        // After unbinding, only this test may own the replaced resource view.
        oldView->AddRef();
        assert(oldView->Release() == 1);
        assert(texture.LoadFromFemory(first.data(), static_cast<int>(first.size())));
        BoundView(texture, 1);
        newView->AddRef();
        assert(newView->Release() == 1);
    }
    Renderer::Uninit();
    Renderer::Uninit();
    std::cout << "Texture decode, failed reload, replacement, and COM lifetime passed.\n";
}
