#pragma once
#include "BallStatus.h"
#include <algorithm>

namespace ShotCombatRules
{
    // Existing collision relic bonuses are additive, not replacements.
    inline int EnemyCollisionDamage(int impactAttack, int bonus)
    {
        return (std::max)(0, impactAttack) + (std::max)(0, bonus);
    }
}
