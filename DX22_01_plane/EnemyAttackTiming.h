#pragma once
#include <algorithm>
#include <cmath>

// The projectile and damage scheduler use the same clock and duration.
namespace EnemyAttackTiming
{
    inline constexpr double FlightSeconds = 0.18;
    inline constexpr double GapSeconds = 0.06;
    inline double Advance(double remaining, double elapsed)
    {
        if (!std::isfinite(elapsed)) return remaining;
        return (std::max)(0.0, remaining - std::clamp(elapsed, 0.0, 0.10));
    }
}
