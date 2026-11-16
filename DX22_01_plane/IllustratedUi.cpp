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

    const ArtTexture& Texture(const char* path)
    {
        auto [entry, inserted] = textures.try_emplace(path);
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
        for (int y = 0; y < art.height; ++y)
            for (int x = 0; x < art.width; ++x)
                if (pixels[(static_cast<std::size_t>(y) * art.width + x) * 4 + 3] >= 24)
                {
                    left = (std::min)(left, x); top = (std::min)(top, y);
                    right = (std::max)(right, x); bottom = (std::max)(bottom, y);
                }
        if (right >= left && bottom >= top)
        {
            art.uvMin = ImVec2(static_cast<float>((std::max)(0, left - 2)) / art.width,
                static_cast<float>((std::max)(0, top - 2)) / art.height);
            art.uvMax = ImVec2(static_cast<float>((std::min)(art.width, right + 3)) / art.width,
                static_cast<float>((std::min)(art.height, bottom + 3)) / art.height);
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

void ResetResources() { textures.clear(); }

void Image(ImDrawList* draw, const char* path, ImVec2 lo, ImVec2 hi, ImU32 tint, bool trimPadding)
{
    const auto& art = Texture(path);
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

Canvas::Canvas(const char* id, float width, float height, ImU32 backdropColor)
{
    const auto* viewport = ImGui::GetMainViewport();
    scale = (std::min)(viewport->WorkSize.x / width, viewport->WorkSize.y / height);
    origin = ImVec2(viewport->WorkPos.x + (viewport->WorkSize.x - width * scale) * 0.5f,
        viewport->WorkPos.y + (viewport->WorkSize.y - height * scale) * 0.5f);
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::GetWindowDrawList()->AddRectFilled(viewport->WorkPos,
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x, viewport->WorkPos.y + viewport->WorkSize.y),
        backdropColor);
}
Canvas::~Canvas() { ImGui::End(); ImGui::PopStyleVar(2); }
ImVec2 Canvas::At(float x, float y) const { return ImVec2(origin.x + x * scale, origin.y + y * scale); }
ImVec2 Canvas::Size(float w, float h) const { return ImVec2(w * scale, h * scale); }
void Canvas::Art(const char* path, float x, float y, float w, float h, ImU32 tint, bool trimPadding) const
{
    Image(ImGui::GetWindowDrawList(), path, At(x, y), At(x + w, y + h), tint, trimPadding);
}
void Canvas::Text(const char* text, float x, float y, float w, float h,
    float fontSize, ImU32 color, bool centered) const
{
    auto* font = ImGui::GetFont();
    float size = fontSize * scale;
    const float wrap = w * scale;
    ImVec2 bounds = font->CalcTextSizeA(size, FLT_MAX, wrap, text);
    if (bounds.y > h * scale && bounds.y > 0)
    {
        size *= h * scale / bounds.y;
        bounds = font->CalcTextSizeA(size, FLT_MAX, wrap, text);
    }
    auto p = At(x, y);
    if (centered) { p.x += (wrap - bounds.x) * 0.5f; p.y += (h * scale - bounds.y) * 0.5f; }
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(At(x, y), At(x + w, y + h), true);
    draw->AddText(font, size, p, color, text, nullptr, wrap);
    draw->PopClipRect();
}
bool Canvas::Hit(const char* id, float x, float y, float w, float h) const
{
    ImGui::SetCursorScreenPos(At(x, y));
    return ImGui::InvisibleButton(id, Size(w, h));
}
void Canvas::Highlight(float x, float y, float w, float h, bool selected, bool hovered, bool enabled) const
{
    auto* draw = ImGui::GetWindowDrawList();
    if (!enabled) draw->AddRectFilled(At(x, y), At(x + w, y + h), IM_COL32(15, 12, 25, 120), 12 * scale);
    if (selected || hovered)
    {
        const auto color = selected ? Gold : Cream;
        draw->AddRect(At(x - 3, y - 3), At(x + w + 3, y + h + 3),
            IM_COL32(255, 196, 78, 52), 14 * scale, 0, 8 * scale);
        draw->AddRect(At(x, y), At(x + w, y + h), color, 12 * scale, 0, (selected ? 3 : 1.5f) * scale);
    }
}
}
