#pragma once

#include "BallPhysicsRules.h"
#include "ShotRelicRules.h"
#include "BossCombatRules.h"
#include <cstdint>
#include <string>
#include <vector>

class Game;
class PlayerBall;
struct PlayerBallData;

namespace BallShotPrediction
{
    struct Ball
    {
        BallPhysicsRules::Body physics;
        DirectX::SimpleMath::Vector3 acceleration = DirectX::SimpleMath::Vector3::Zero;
        int hp = 0, maxHp = 1, attack = 0, defense = 0;
        std::string enemyId;
        float frontalMultiplier = 1.0f, pocketDamageRatio = 0.0f;
        bool active = true, defeated = false, pocketed = false, playerSimulation = true;
        int stopCount = 0;
        BossCombatRules::State bossState;
        bool breakBallUsed = false;
    };

    struct Result
    {
        std::vector<DirectX::SimpleMath::Vector3> path;
        std::vector<Ball> balls;
        ShotRelicRules shot;
        bool complete = false, hitBall = false, pathTruncated = false;
        int ticks = 0, substeps = 0, damage = 0;
        int bossFixedDamage = 0, breakBallHits = 0;
        DirectX::SimpleMath::Vector3 firstPlayerContact = DirectX::SimpleMath::Vector3::Zero;
        DirectX::SimpleMath::Vector3 firstEnemyContact = DirectX::SimpleMath::Vector3::Zero;
        DirectX::SimpleMath::Vector3 firstEnemyDirection = DirectX::SimpleMath::Vector3::Zero;
        double milliseconds = 0.0;
    };

    // Predict through the last physics tick, before healing/respawn/enemy turn.
    // Mouse launches preserve the existing first-tick friction skip; AI launches do not.
    Result Predict(Game& game, const PlayerBall& player,
        const DirectX::SimpleMath::Vector3& velocity, bool skipFirstPlayerFriction, const PlayerBallData* offer = nullptr);
    std::uint64_t WorldKey(Game& game);

    // Debug-only, opt-in isolated runtime comparison; never invoked in normal play.
    void WriteVerificationPrediction(Game& game, const PlayerBall& player,
        const DirectX::SimpleMath::Vector3& velocity, bool skipFirstPlayerFriction);
    void WriteVerificationActual(Game& game);
}
