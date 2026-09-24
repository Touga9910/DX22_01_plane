#pragma once

#include "TableConfig.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace PocketReturnRules
{
    inline DirectX::SimpleMath::Vector3 ClosestPocketCenter(
        const DirectX::SimpleMath::Vector3& entryPosition)
    {
        const auto pocketCenters = TableConfig::GetPocketCenters();
        DirectX::SimpleMath::Vector3 closest = pocketCenters.front();
        float closestDistanceSquared = (std::numeric_limits<float>::max)();
        for (const auto& center : pocketCenters)
        {
            const float dx = entryPosition.x - center.x;
            const float dz = entryPosition.z - center.z;
            const float distanceSquared = dx * dx + dz * dz;
            if (distanceSquared < closestDistanceSquared)
            {
                closest = center;
                closestDistanceSquared = distanceSquared;
            }
        }
        return closest;
    }

    inline DirectX::SimpleMath::Vector3 InwardDirection(
        const DirectX::SimpleMath::Vector3& entryPosition)
    {
        const auto pocketCenter = ClosestPocketCenter(entryPosition);
        const float length = std::sqrt(
            pocketCenter.x * pocketCenter.x +
            pocketCenter.z * pocketCenter.z);
        if (length <= 0.0001f)
        {
            return DirectX::SimpleMath::Vector3(0.0f, 0.0f, -1.0f);
        }
        return DirectX::SimpleMath::Vector3(
            -pocketCenter.x / length,
            0.0f,
            -pocketCenter.z / length);
    }

    inline DirectX::SimpleMath::Vector3 ReturnAnchor(
        const DirectX::SimpleMath::Vector3& entryPosition,
        float inwardDistance)
    {
        const auto pocketCenter = ClosestPocketCenter(entryPosition);
        const auto inward = InwardDirection(entryPosition);
        const float distance = (std::max)(0.0f, inwardDistance);
        return DirectX::SimpleMath::Vector3(
            pocketCenter.x + inward.x * distance,
            TableConfig::FIELD_HEIGHT,
            pocketCenter.z + inward.z * distance);
    }
}
