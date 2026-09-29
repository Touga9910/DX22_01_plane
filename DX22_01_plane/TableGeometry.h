#pragma once

#include "TableConfig.h"
#include "SimpleMath.h"

#include <array>

// Keep vector-based geometry separate from the lightweight dimensions header.
namespace TableConfig
{
    inline std::array<DirectX::SimpleMath::Vector3, 6>
        GetPocketCenters()
    {
        const float halfWidth = GetFieldWidth() * 0.5f;
        const float halfDepth = GetFieldDepth() * 0.5f;
        constexpr float y = 0.05f;
        return {
            DirectX::SimpleMath::Vector3(-halfWidth, y, halfDepth),
            DirectX::SimpleMath::Vector3(0.0f, y, halfDepth),
            DirectX::SimpleMath::Vector3(halfWidth, y, halfDepth),
            DirectX::SimpleMath::Vector3(-halfWidth, y, -halfDepth),
            DirectX::SimpleMath::Vector3(0.0f, y, -halfDepth),
            DirectX::SimpleMath::Vector3(halfWidth, y, -halfDepth),
        };
    }
}
