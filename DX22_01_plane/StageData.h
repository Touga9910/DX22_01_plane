#pragma once

#include <string>
#include <vector>
#include "EnemyData.h"

enum class StageType
{
    Normal,
    MidBoss,
    Boss
};

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
};

struct StageData
{
    std::string id;
    StageType stageType = StageType::Normal;
    int difficulty = 1;
    int par = 4;
    bool preserveLayout = false; // Authored stages bypass automatic dense arrangement.

    std::vector<EnemySpawnData> enemies;
};
