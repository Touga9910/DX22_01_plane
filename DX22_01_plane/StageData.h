#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include "EnemyData.h"
#include "EnemyValidationSettings.h"
#include "TableConfig.h"

enum class StageType
{
    Normal,
    MidBoss,
    Boss
};

enum class CombatMode
{
    Legacy,
    PhasePrototype
};

/// <summary>
/// 戦闘モードをデータに保存する識別子へ変換する。
/// </summary>
inline const char* ToString(CombatMode mode)
{
    return mode == CombatMode::PhasePrototype
        ? "phase_prototype"
        : "legacy";
}

/// <summary>
/// ステージ種別をデータに保存する識別子へ変換する。
/// </summary>
inline const char* ToString(StageType stageType)
{
    switch (stageType)
    {
    case StageType::Normal:
        return "normal";
    case StageType::MidBoss:
        return "midBoss";
    case StageType::Boss:
        return "boss";
    default:
        return "normal";
    }
}

struct EnemySpawnData
{
    DirectX::SimpleMath::Vector3 position =
        DirectX::SimpleMath::Vector3::Zero;

    // JSONでは省略可能。省略時はenemy_normalを使用する。
    std::string enemyId = "enemy_normal";
    EnemyData enemyData;
    // 0 means to use the phase battle's default interval.
    int attackInterval = 0;
    // Positive values keep phase reinforcements in the same relative layout.
    int spawnGroup = 0;
};

struct EnemyPhaseData
{
    std::vector<EnemySpawnData> enemies;
};

struct PhaseBattleData
{
    int turnsPerPhase = 2;
    int defaultAttackInterval = 2;
    unsigned int spawnSeed = 20261114u;
    std::vector<EnemyPhaseData> phases;
};

/// <summary>
/// ステージ名に初期配置と各増援フェーズの敵数を付けた表示名を組み立てる。
/// </summary>
inline std::string FormatStageDisplayName(
    std::string_view name,
    std::size_t openingEnemyCount,
    const std::vector<std::size_t>& reinforcementCounts)
{
    std::string result(name);
    result += '_';
    result += std::to_string(openingEnemyCount);
    for (const std::size_t count : reinforcementCounts)
    {
        result += '*';
        result += std::to_string(count);
    }
    return result;
}

struct StageData
{
    std::string id;
    std::string name;
    StageType stageType = StageType::Normal;
    int difficulty = 1;
    int par = 4;
    bool preserveLayout = false; // Authored stages bypass automatic dense arrangement.
    bool hasBreakBallLayout = false;
    bool prototypeOnly = false; // Excluded from normal roguelike selection.
    CombatMode combatMode = CombatMode::Legacy;

    std::vector<EnemySpawnData> enemies;
    PhaseBattleData phaseBattle;
    std::vector<DirectX::SimpleMath::Vector3> breakBallPositions;
    std::optional<EnemyValidationSettings> enemyValidation;

    /// <summary>
    /// ステージの初期配置と増援フェーズの敵数を含む表示名を返す。
    /// </summary>
    std::string DisplayName() const
    {
        std::vector<std::size_t> reinforcementCounts;
        for (std::size_t i = 1; i < phaseBattle.phases.size(); ++i)
            reinforcementCounts.push_back(phaseBattle.phases[i].enemies.size());
        return FormatStageDisplayName(name.empty() ? id : name, enemies.size(), reinforcementCounts);
    }
};

/// <summary>
/// ボス戦のブレイクボールの既定座標を返す。
/// </summary>
inline std::vector<DirectX::SimpleMath::Vector3> DefaultBossBreakBallPositions()
{
    return {
        {-4.0f, TableConfig::FIELD_HEIGHT, 10.0f},
        {4.0f, TableConfig::FIELD_HEIGHT, 10.0f}
    };
}
