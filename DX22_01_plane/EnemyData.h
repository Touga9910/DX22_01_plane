#pragma once

#include <string>
#include <vector>
#include "utility.h"
#include "BallStatus.h"
#include "StatusEffect.h"
#include "MeshRenderer.h"

struct NuisanceBallData
{
    bool enabled = false;
    int initialDelayTurns = 2;
    int respawnDelayTurns = 3;
    DirectX::SimpleMath::Vector3 spawnOffset =
        DirectX::SimpleMath::Vector3(12.0f, 0.0f, 0.0f);
    float radius = 2.0f;
    float mass = 1.0f;
    float restitution = 0.8f;
    float friction = 0.025f;
    StatusEffectCollection debuffs;
};

struct EnemyData
{
    std::string id = "enemy_default";

    std::string modelFilePath = "assets/model/GolfBall/golf_ball.obj";
    std::string textureDirectory = "assets/model/GolfBall";

    int maxHp = 3;
    BallStatus status;
    // 敵マスタではなく、ステージ上の各配置が個別に設定する初期状態効果。
    StatusEffectCollection initialStatusEffects;
    float frontalDamageMultiplier = 1.0f;
    float pocketDamageRatio = 0.0f;
    // Empty means this enemy has no collision-stage vulnerability gimmick.
    // The last multiplier remains active after the final stage is reached.
    std::vector<float> collisionDamageMultipliers;
    int collisionCountGraceTicks = 6;
    NuisanceBallData nuisanceBall;

    DirectX::SimpleMath::Vector3 initPosition =
        DirectX::SimpleMath::Vector3(50.0f, 0.0f, 50.0f);

    DirectX::SimpleMath::Vector3 scale =
        DirectX::SimpleMath::Vector3(2.4f, 2.4f, 2.4f);

    int rewardMoney = 0;
    int rewardExp = 0;
};
