#pragma once
#include "imgui/imgui.h"
#include <algorithm>
#include <string>
#include <unordered_map>

namespace GameUi
{
    inline unsigned layoutRevision = 0;
    inline bool showDebugger = false;
    inline std::unordered_map<std::string, unsigned> windowRevisions;

    inline ImVec2 MainPosition(float x, float y)
    {
        const auto* viewport = ImGui::GetMainViewport();
        return ImVec2(viewport->WorkPos.x + x, viewport->WorkPos.y + y);
    }

    inline void ResetWindows() { ++layoutRevision; }

    inline void PrepareWindow(const char* id, ImVec2 offset, ImVec2 size)
    {
        const auto* viewport = ImGui::GetMainViewport();
        const bool reset = windowRevisions[id] != layoutRevision;
        windowRevisions[id] = layoutRevision;
        size.x = (std::min)(size.x, (std::max)(300.0f, viewport->WorkSize.x - 24.0f));
        size.y = (std::min)(size.y, (std::max)(200.0f, viewport->WorkSize.y - 24.0f));
        offset.x = std::clamp(offset.x, 12.0f, (std::max)(12.0f, viewport->WorkSize.x - size.x - 12.0f));
        offset.y = std::clamp(offset.y, 12.0f, (std::max)(12.0f, viewport->WorkSize.y - size.y - 12.0f));
        const ImGuiCond condition = reset ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
        if (reset)
        {
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        }
        ImGui::SetNextWindowPos(MainPosition(offset.x, offset.y), condition);
        ImGui::SetNextWindowSize(size, condition);
    }
}
