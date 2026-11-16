#pragma once

#include "imgui/imgui.h"
#include <cstdint>
#include <utility>
#include <vector>

struct EnemyInfoLink
{
    ImVec2 start;
    ImVec2 end;
};

struct EnemyInfoPlacement
{
    ImVec2 position;
    EnemyInfoLink link{};
    bool moved = false;
    bool hidden = false;
};

// One frame's placement reservations. This has no Game, camera, or renderer
// dependency; projected body regions and viewport bounds are explicit inputs.
class EnemyInfoLayout final
{
public:
    using BodyRegions = std::vector<std::pair<std::uintptr_t, ImVec4>>;
    EnemyInfoLayout(ImVec2 position, ImVec2 size, float safeTop, const BodyRegions& bodies)
        : m_ViewportPosition(position), m_ViewportSize(size),
          m_SafeTop(safeTop), m_BodyRegions(bodies) {}

    ImVec2 ClampHoverPosition(const ImVec2& base, float width, float height) const;
    EnemyInfoLink MakeLink(const ImVec2& pos, float width, float height,
        bool upper, const ImVec2& ball, float radius) const;
    EnemyInfoPlacement Resolve(const ImVec2& base, float width, float height,
        bool upper, std::uintptr_t owner, const ImVec2& ball, float radius);

private:
    ImVec4 MakeFrameRect(const ImVec2& pos, float width, float height) const;
    static float PointSegmentDistanceSquared(const ImVec2& point,
        const ImVec2& a, const ImVec2& b);

    ImVec2 m_ViewportPosition;
    ImVec2 m_ViewportSize;
    float m_SafeTop;
    const BodyRegions& m_BodyRegions;
    std::vector<ImVec4> m_OccupiedFrames;
    std::vector<EnemyInfoLink> m_OccupiedLinks;
};
