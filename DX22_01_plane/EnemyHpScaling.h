#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace EnemyHpScaling
{
    /// <summary>
    /// 敵の基準最大HPに倍率を適用し、入力値と結果を使用可能な範囲に収める。
    /// </summary>
    inline int ApplyMultiplier(int baseMaxHp, float multiplier)
    {
        const double safeBase = static_cast<double>((std::max)(1, baseMaxHp));
        if (!std::isfinite(multiplier) || multiplier <= 0.0f)
        {
            return static_cast<int>(safeBase);
        }

        const double scaled = std::ceil(
            safeBase * static_cast<double>(multiplier));
        const double maximum = static_cast<double>(
            (std::numeric_limits<int>::max)());
        if (!std::isfinite(scaled) || scaled >= maximum)
        {
            return (std::numeric_limits<int>::max)();
        }
        return (std::max)(1, static_cast<int>(scaled));
    }
}
