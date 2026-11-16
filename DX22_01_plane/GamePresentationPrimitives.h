#pragma once

struct ImVec2;
namespace DirectX::SimpleMath { struct Vector3; }
enum class AxisResourceKind;
enum class AxisResourceChangeReason;

// Implementation helpers shared by presentation translation units only.
// Game rules and runtime state do not depend on ImGui or GPU resources here.
namespace GamePresentationPrimitives
{
    const char* PresentationUtf8(const char8_t* text) noexcept;
    unsigned int WithAlpha(unsigned int color, float alpha);
    const char* AxisResourceLabel(AxisResourceKind axis);
    unsigned int AxisResourceColor(AxisResourceKind axis);
    const char* AxisResourceReasonLabel(AxisResourceChangeReason reason);
    bool TryProjectToScreen(
        const DirectX::SimpleMath::Vector3& worldPosition, ImVec2& screenPosition);
}
