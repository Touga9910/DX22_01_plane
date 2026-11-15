#pragma once

#include <string>
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

inline const char* ToString(CombatMode mode)
{
    return mode == CombatMode::PhasePrototype
        ? "phase_prototype"
        : "legacy";
}

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

struct StageData
{
    std::string id;
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
};

inline std::vector<DirectX::SimpleMath::Vector3> DefaultBossBreakBallPositions()
{
    return {
        {-4.0f, TableConfig::FIELD_HEIGHT, 10.0f},
        {4.0f, TableConfig::FIELD_HEIGHT, 10.0f}
    };
}
