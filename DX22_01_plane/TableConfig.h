#pragma once

#include "SimpleMath.h"

#include <array>

namespace TableConfig
{
    // Keep the playable cloth at the standard 2:1 pool-table proportion.
    static constexpr float TABLE_OUTER_WIDTH = 152.0f;
    static constexpr float TABLE_OUTER_DEPTH = 80.0f;
    static constexpr float RAIL_WIDTH = 4.0f;
    static constexpr float POCKET_RADIUS = 3.0f;
    static constexpr float CORNER_POCKET_MOUTH_HALF_WIDTH = 3.5f;
    static constexpr float SIDE_POCKET_MOUTH_HALF_WIDTH = 3.75f;
    static constexpr float FIELD_HEIGHT = 1.0f;
    static constexpr float RAIL_TOP_OFFSET = 0.05f;

    inline float GetFieldWidth()
    {
        return TABLE_OUTER_WIDTH - RAIL_WIDTH * 2.0f;
    }

    inline float GetFieldDepth()
    {
        return TABLE_OUTER_DEPTH - RAIL_WIDTH * 2.0f;
    }

    inline std::array<DirectX::SimpleMath::Vector3, 6>
        GetPocketCenters()
    {
        const float halfWidth = GetFieldWidth() * 0.5f;
        const float halfDepth = GetFieldDepth() * 0.5f;
        const float y = 0.05f;
        return {
            DirectX::SimpleMath::Vector3(-halfWidth, y, halfDepth),
            DirectX::SimpleMath::Vector3(0.0f, y, halfDepth),
            DirectX::SimpleMath::Vector3(halfWidth, y, halfDepth),
            DirectX::SimpleMath::Vector3(-halfWidth, y, -halfDepth),
            DirectX::SimpleMath::Vector3(0.0f, y, -halfDepth),
            DirectX::SimpleMath::Vector3(halfWidth, y, -halfDepth),
        };
    }

    inline bool IsDepthLongSide()
    {
        return GetFieldDepth() >= GetFieldWidth();
    }
}

static_assert(
    TableConfig::TABLE_OUTER_WIDTH - TableConfig::RAIL_WIDTH * 2.0f ==
        (TableConfig::TABLE_OUTER_DEPTH - TableConfig::RAIL_WIDTH * 2.0f) * 2.0f,
    "The billiard playing surface must keep a 2:1 aspect ratio.");
