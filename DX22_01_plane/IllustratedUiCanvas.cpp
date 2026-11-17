#include "IllustratedUi.h"
#include <algorithm>
#include <cfloat>

namespace IllustratedUi
{
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
