#pragma once

#include "BallStatus.h"
#include <cmath>
#include <utility>

namespace BallMechanics
{
    // Return the two normal velocities after a collision. Tangential velocity is unchanged.
    inline std::pair<float, float> ResolveNormalImpact(
        float first, float second, float firstMass, float secondMass,
        float restitution, float firstTransfer, float secondTransfer,
        bool firstLocked, bool secondLocked)
    {
        if (firstLocked) return { first, -second * restitution };
        if (secondLocked) return { -first * restitution, second };
        const float totalMass = firstMass + secondMass;
        const float relative = first - second;
        return {
            first - (2.0f * secondMass / totalMass) * relative * restitution * secondTransfer,
            second + (2.0f * firstMass / totalMass) * relative * restitution * firstTransfer
        };
    }

    inline int PierceUses(const BallStatus& status, bool charged)
    {
        return status.abilities.pierce ? status.pierceMaxUses + (charged ? 1 : 0) : 0;
    }

    inline float PierceRetention(const BallStatus& status, bool charged)
    {
        return charged ? 1.0f : status.pierceSpeedRetention;
    }

    inline int DirectionalDamage(int damage, float facingDot, float multiplier)
    {
        return facingDot >= 0.5f
            ? (std::max)(1, static_cast<int>(std::ceil(damage * multiplier)))
            : damage;
    }

    inline int PocketDamage(int maxHp, float ratio)
    {
        return ratio > 0.0f ? (std::max)(1, static_cast<int>(std::ceil(maxHp * ratio))) : 0;
    }
}
