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

/// <summary>
/// 敵の検証用配置パターンを設定保存用の識別文字列に変換する。
/// </summary>
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

/// <summary>
/// 設定文字列を対応する敵の配置パターンに変換し、認識できたかを返す。
/// </summary>
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

/// <summary>
/// 敵の検証用HP倍率が対応値の1倍、1.5倍、2倍のいずれかに近いか確認する。
/// </summary>
inline bool IsSupportedEnemyHpMultiplier(float multiplier)
{
    return std::isfinite(multiplier) &&
        (std::abs(multiplier - 1.0f) < 0.0001f ||
         std::abs(multiplier - 1.5f) < 0.0001f ||
         std::abs(multiplier - 2.0f) < 0.0001f);
}

/// <summary>
/// 検証で配置する敵の数が、対応している6体、8体、10体、12体のいずれかかを確認する。
/// </summary>
inline bool IsSupportedEnemyValidationCount(int count)
{
	return count == 6 || count == 8 || count == 10 || count == 12;
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
