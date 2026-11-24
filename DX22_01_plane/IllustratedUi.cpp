#include "IllustratedUi.h"
#include "Renderer.h"
#include "stb_image.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <wrl/client.h>

namespace IllustratedUi
{
namespace
{
    struct ArtTexture
    {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        int width = 0, height = 0;
        ImVec2 uvMin{0, 0}, uvMax{1, 1};
    };
    std::unordered_map<std::string, ArtTexture> textures;

    /// <summary>
    /// 画像を必要に応じて変換してGPUへ転送し、読み込み結果をパスと表示形式でキャッシュする。
    /// </summary>
    const ArtTexture& Texture(const char* path, bool grayscale = false)
    {
        const std::string key = std::string(path) + (grayscale ? "#grayscale" : "");
        auto [entry, inserted] = textures.try_emplace(key);
        if (!inserted) return entry->second;
        auto& art = entry->second;
        int channels = 0;
        unsigned char* pixels = stbi_load(path, &art.width, &art.height, &channels, 4);
        if (!pixels)
        {
            std::cerr << "UI artwork load failed: " << path << '\n';
            return art;
        }
        // Use the existing alpha channel to ignore export padding. Originals
        // remain untouched, and opaque backgrounds keep their full extent.
        int left = art.width, top = art.height, right = -1, bottom = -1;
        const bool cardIcon = std::string(path).find("/Cards/card_") != std::string::npos;
        for (int y = 0; y < art.height; ++y)
            for (int x = 0; x < art.width; ++x)
            {
                const auto offset = (static_cast<std::size_t>(y) * art.width + x) * 4;
                const bool blackGutter = cardIcon && pixels[offset] <= 3 &&
                    pixels[offset + 1] <= 3 && pixels[offset + 2] <= 3;
                if (pixels[offset + 3] >= 24 && !blackGutter)
                {
                    left = (std::min)(left, x); top = (std::min)(top, y);
                    right = (std::max)(right, x); bottom = (std::max)(bottom, y);
                }
            }
        if (right >= left && bottom >= top)
        {
            art.uvMin = ImVec2(static_cast<float>((std::max)(0, left - 2)) / art.width,
                static_cast<float>((std::max)(0, top - 2)) / art.height);
            art.uvMax = ImVec2(static_cast<float>((std::min)(art.width, right + 3)) / art.width,
                static_cast<float>((std::min)(art.height, bottom + 3)) / art.height);
        }
        if (grayscale)
            for (std::size_t i = 0; i < static_cast<std::size_t>(art.width) * art.height * 4; i += 4)
            {
                const auto gray = static_cast<unsigned char>(
                    (54u * pixels[i] + 183u * pixels[i + 1] + 19u * pixels[i + 2]) / 256u);
                pixels[i] = pixels[i + 1] = pixels[i + 2] = gray;
            }
        D3D11_TEXTURE2D_DESC description{};
        description.Width = art.width; description.Height = art.height;
        description.MipLevels = 1; description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = pixels; data.SysMemPitch = art.width * 4;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        auto* device = Renderer::GetDevice();
        HRESULT result = device ? device->CreateTexture2D(&description, &data, &texture) : E_FAIL;
        if (SUCCEEDED(result)) result = device->CreateShaderResourceView(texture.Get(), nullptr, &art.view);
        stbi_image_free(pixels);
        if (FAILED(result)) std::cerr << "UI artwork upload failed: " << path << '\n';
        return art;
    }
}

/// <summary>
/// イラストのキャッシュを消去してテクスチャを解放する。
/// </summary>
void ResetResources() { textures.clear(); }

/// <summary>
/// 指定領域に画像を描き、必要に応じて余白を除いたUV範囲や代替枠を使う。
/// </summary>
void Image(ImDrawList* draw, const char* path, ImVec2 lo, ImVec2 hi, ImU32 tint, bool trimPadding, bool grayscale)
{
    const auto& art = Texture(path, grayscale);
    if (!art.view)
    {
        draw->AddRectFilled(lo, hi, IM_COL32(36, 27, 46, 255), 6);
        draw->AddRect(lo, hi, Gold, 6);
        return;
    }
    const ImVec2 uvMin = trimPadding ? art.uvMin : ImVec2(0, 0);
    const ImVec2 uvMax = trimPadding ? art.uvMax : ImVec2(1, 1);
    const float w = art.width * (uvMax.x - uvMin.x);
    const float h = art.height * (uvMax.y - uvMin.y);
    const float scale = (std::min)((hi.x - lo.x) / w, (hi.y - lo.y) / h);
    const ImVec2 start(lo.x + ((hi.x - lo.x) - w * scale) * 0.5f,
        lo.y + ((hi.y - lo.y) - h * scale) * 0.5f);
    draw->AddImage(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(art.view.Get()))),
        start, ImVec2(start.x + w * scale, start.y + h * scale), uvMin, uvMax, tint);
}

/// <summary>
/// 指定されたUV範囲の画像を描き、読み込めない場合は代替領域を描く。
/// </summary>
void ImageRegion(ImDrawList* draw, const char* path, ImVec2 lo, ImVec2 hi,
    ImVec2 uvMin, ImVec2 uvMax, bool grayscale)
{
    const auto& art = Texture(path, grayscale);
    if (!art.view) { draw->AddRectFilled(lo, hi, IM_COL32(90, 90, 90, 255)); return; }
    draw->AddImage(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(art.view.Get()))),
        lo, hi, uvMin, uvMax);
}

}
