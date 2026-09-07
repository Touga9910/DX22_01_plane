#pragma once
#include "PlayerBallText.h"
#include "imgui/imgui.h"

namespace PlayerBallUI
{
    inline bool Select(const PlayerBallData& ball, bool selected)
    {
        const auto color = PlayerBallText::GetColor(ball.definitionId);
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float rowHeight = ImGui::GetTextLineHeightWithSpacing() + 8.0f;
        std::string label = "      " + std::string(PlayerBallText::GetName(ball.definitionId)) +
            "  +" + std::to_string(ball.upgradeLevel);
        if (ball.instanceId != 0) label += "  / No." + std::to_string(ball.instanceId);
        const bool clicked = ImGui::Selectable(label.c_str(), selected, 0, ImVec2(0, rowHeight));
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(start.x + 11.0f, start.y + rowHeight * 0.5f), 8.0f,
            ImGui::ColorConvertFloat4ToU32(ImVec4(color[0], color[1], color[2], 1.0f)));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", PlayerBallText::GetDescription(ball.definitionId));
        return clicked;
    }
}
