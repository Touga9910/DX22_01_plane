#pragma once

#include <cmath>
#include <cstdint>
#include <string>

enum class EnemyValidationLayoutPattern
{
    Dense,
    Line,
    Random,
};

inline const char* EnemyValidationLayoutPatternId(
    EnemyValidationLayoutPattern pattern)
{
    switch (pattern)
    {
    case EnemyValidationLayoutPattern::Dense: return "dense";
    case EnemyValidationLayoutPattern::Line: return "line";
    default: return "random";
    }
}

inline bool TryParseEnemyValidationLayoutPattern(
    const std::string& id,
    EnemyValidationLayoutPattern& pattern)
{
    if (id == "dense") pattern = EnemyValidationLayoutPattern::Dense;
    else if (id == "line") pattern = EnemyValidationLayoutPattern::Line;
    else if (id == "random") pattern = EnemyValidationLayoutPattern::Random;
    else return false;
    return true;
}

inline bool IsSupportedEnemyHpMultiplier(float multiplier)
{
    return std::isfinite(multiplier) &&
        (std::abs(multiplier - 1.0f) < 0.0001f ||
         std::abs(multiplier - 1.5f) < 0.0001f ||
         std::abs(multiplier - 2.0f) < 0.0001f);
}

inline bool IsSupportedEnemyValidationCount(int count)
{
    return count == 6 || count == 8 || count == 10;
}

// DebugBattleSetup owns this value. StageData only carries the active snapshot
// so battle initialization and logging use the same immutable settings.
struct EnemyValidationSettings
{
    bool enabled = false;
    float hpMultiplier = 1.0f;
    int enemyCount = 8;
    EnemyValidationLayoutPattern layoutPattern =
        EnemyValidationLayoutPattern::Random;
    std::uint32_t seed = 20260904;
};
