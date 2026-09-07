#pragma once

#include "SimpleMath.h"

#include <array>

namespace TableConfig
{
    static constexpr float TABLE_OUTER_WIDTH = 145.0f;
    static constexpr float TABLE_OUTER_DEPTH = 80.0f;
    static constexpr float RAIL_WIDTH = 4.0f;
    static constexpr float POCKET_RADIUS = 3.0f;
    static constexpr float POCKET_MOUTH_HALF_WIDTH = 4.5f;
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
