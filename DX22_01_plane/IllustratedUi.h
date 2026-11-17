#pragma once

#include "imgui/imgui.h"

// Shared, aspect-preserving canvas for the supplied map and reward artwork.
namespace IllustratedUi
{
    constexpr ImU32 Cream = IM_COL32(255, 239, 198, 255);
    constexpr ImU32 Gold = IM_COL32(255, 207, 103, 255);
    constexpr ImU32 Ink = IM_COL32(42, 27, 49, 255);

    void ResetResources();
    void Image(ImDrawList* draw, const char* path, ImVec2 lo, ImVec2 hi,
        ImU32 tint = IM_COL32_WHITE, bool trimPadding = true, bool grayscale = false);
    void ImageRegion(ImDrawList* draw, const char* path, ImVec2 lo, ImVec2 hi,
        ImVec2 uvMin, ImVec2 uvMax, bool grayscale = false);

    struct Canvas
    {
        ImVec2 origin;
        float scale = 1.0f;
        Canvas(const char* id, float width, float height,
            ImU32 backdropColor = IM_COL32(16, 16, 30, 255));
        ~Canvas();
        ImVec2 At(float x, float y) const;
        ImVec2 Size(float width, float height) const;
        void Art(const char* path, float x, float y, float width, float height,
            ImU32 tint = IM_COL32_WHITE, bool trimPadding = true) const;
        void Text(const char* text, float x, float y, float width, float height,
            float fontSize, ImU32 color = Cream, bool centered = true) const;
        bool Hit(const char* id, float x, float y, float width, float height) const;
        void Highlight(float x, float y, float width, float height, bool selected,
            bool hovered, bool enabled = true) const;
    };
}
