#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace EnemyGimmickRules
{
    /// <summary>
    /// 衝突回数に対応するダメージ倍率を選び、未設定なら等倍を返す。
    /// </summary>
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

    /// <summary>
    /// 衝突ダメージに段階倍率を適用し、正の結果を四捨五入して最低1にする。
    /// </summary>
    inline int ScaleCollisionDamage(int damage, float multiplier)
    {
        if (damage <= 0 || multiplier <= 0.0f) return 0;
        return (std::max)(1, static_cast<int>(damage * multiplier + 0.5f));
    }

    /// <summary>
    /// 設定された衝突段階における進行割合から、敵球の表示の明るさを求める。
    /// </summary>
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
