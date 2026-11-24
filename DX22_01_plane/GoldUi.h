#pragma once

#include "imgui/imgui.h"
#include <cfloat>
#include <string>

// One reusable gold symbol + amount, for prices and owned-money displays.
namespace GoldUi
{
    /// <summary>
    /// ゴールドのアイコンと金額の文字列を表示するのに必要な幅を求める。
    /// </summary>
    inline float Width(int amount, float height, float fontSize)
    {
        const std::string number = std::to_string(amount);
        return height * 1.22f + ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0, number.c_str()).x;
    }

    /// <summary>
    /// ゴールドのアイコンと金額を指定位置へ描画し、必要に応じてアイコンをグレースケールにする。
    /// </summary>
    inline void Draw(ImDrawList* draw, ImVec2 position, int amount, float height,
        float fontSize, ImU32 textColor = IM_COL32(255, 225, 140, 255), bool grayscale = false)
    {
        const ImU32 edge = grayscale ? IM_COL32(70, 70, 70, 255) : IM_COL32(98, 51, 12, 255);
        const ImU32 side = grayscale ? IM_COL32(140, 140, 140, 255) : IM_COL32(220, 139, 15, 255);
        const ImU32 face = grayscale ? IM_COL32(210, 210, 210, 255) : IM_COL32(255, 217, 65, 255);
        const ImU32 rim = grayscale ? IM_COL32(245, 245, 245, 255) : IM_COL32(255, 246, 173, 255);
        auto coin = [&](float x, float y)
        {
            ImVec2 center(position.x + x * height, position.y + y * height);
            const ImVec2 radius(height * 0.27f, height * 0.12f);
            draw->AddRectFilled(ImVec2(center.x - radius.x, center.y),
                ImVec2(center.x + radius.x, center.y + height * 0.22f), side);
            draw->AddEllipseFilled(ImVec2(center.x, center.y + height * 0.22f), radius, side, 0, 20);
            draw->AddEllipseFilled(center, radius, face, 0, 20);
            draw->AddEllipse(center, radius, edge, 0, 20, height * 0.025f);
            draw->AddEllipse(center, ImVec2(radius.x * 0.78f, radius.y * 0.72f), rim, 0, 20, height * 0.035f);
        };
        coin(0.29f, 0.60f); coin(0.75f, 0.60f); coin(0.53f, 0.26f);
        const std::string number = std::to_string(amount);
        draw->AddText(ImGui::GetFont(), fontSize,
            ImVec2(position.x + height * 1.22f, position.y + (height - fontSize) * 0.5f),
            textColor, number.c_str());
    }
}
