#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace EnemyGimmickRules
{
    inline float CollisionDamageMultiplier(
        const std::vector<float>& multipliers,
        int collisionCount)
    {
        if (multipliers.empty()) return 1.0f;
        const std::size_t index = (std::min)(
            static_cast<std::size_t>((std::max)(0, collisionCount)),
            multipliers.size() - 1);
        return (std::max)(0.0f, multipliers[index]);
    }

    inline int ScaleCollisionDamage(int damage, float multiplier)
    {
        if (damage <= 0 || multiplier <= 0.0f) return 0;
        return (std::max)(1, static_cast<int>(damage * multiplier + 0.5f));
    }

    inline float CollisionStageBrightness(
        const std::vector<float>& multipliers,
        int collisionCount)
    {
        if (multipliers.size() <= 1) return 1.0f;
        const float progress = static_cast<float>((std::min)(
            static_cast<std::size_t>((std::max)(0, collisionCount)),
            multipliers.size() - 1)) /
            static_cast<float>(multipliers.size() - 1);
        return 1.0f - progress * 0.65f;
    }
}
