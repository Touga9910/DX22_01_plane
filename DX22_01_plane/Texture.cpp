#include "Texture.h"
#include "stb_image.h"
#include "renderer.h"

#include <iostream>
#include <memory>
#include <utility>

namespace
{
    using ImagePixels = std::unique_ptr<unsigned char, decltype(&stbi_image_free)>;
}

// Keep the previous texture intact when decoding or GPU allocation fails.
bool Texture::Load(const std::string& filename)
{
    int width{}, height{}, channels{};
    ImagePixels pixels(stbi_load(filename.c_str(), &width, &height, &channels, 4),
        &stbi_image_free);
    if (!pixels)
    {
        std::cout << filename << " Load error " << std::endl;
        return false;
    }
    return UploadRgba(pixels.get(), width, height, channels);
}

bool Texture::LoadFromFemory(const unsigned char* data, int len)
{
    if (data == nullptr || len <= 0) return false;
    int width{}, height{}, channels{};
    ImagePixels pixels(stbi_load_from_memory(data, len, &width, &height, &channels,
        STBI_rgb_alpha), &stbi_image_free);
    if (!pixels) return false;
    return UploadRgba(pixels.get(), width, height, channels);
}

bool Texture::UploadRgba(const unsigned char* pixels, int width, int height, int channels)
{
    ID3D11Device* device = Renderer::GetDevice();
    if (device == nullptr || pixels == nullptr || width <= 0 || height <= 0) return false;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = pixels;
    data.SysMemPitch = desc.Width * 4;
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&desc, &data, texture.GetAddressOf()))) return false;

    ComPtr<ID3D11ShaderResourceView> view;
    if (FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf()))) return false;
    m_srv = std::move(view);
    m_width = width;
    m_height = height;
    m_bpp = channels;
    return true;
}

void Texture::SetGPU()
{
    if (ID3D11DeviceContext* context = Renderer::GetDeviceContext())
        context->PSSetShaderResources(0, 1, m_srv.GetAddressOf());
}
