#pragma once
#include "BallStatus.h"
#include <algorithm>

namespace ShotCombatRules
{
    // Existing collision relic bonuses are additive, not replacements.
    /// <summary>
    /// 追突力とレリックの加算値をそれぞれ0以上として、敵同士の衝突ダメージを計算する。
    /// </summary>
    inline int EnemyCollisionDamage(int impactAttack, int bonus)
    {
        return (std::max)(0, impactAttack) + (std::max)(0, bonus);
    }
}
