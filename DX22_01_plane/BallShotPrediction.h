#pragma once

#include "BallPhysicsRules.h"
#include "ShotRelicRules.h"
#include "BossCombatRules.h"
#include "CushionChargeRules.h"
#include "HeavyCollisionRules.h"
#include "PierceTraceRules.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class BallComponent;
class Pocket;
class PlayerBall;
class TableFrame;
struct PlayerBallData;

namespace BallShotPrediction
{
    struct Ball
    {
        BallPhysicsRules::Body physics;
        DirectX::SimpleMath::Vector3 acceleration = DirectX::SimpleMath::Vector3::Zero;
        int hp = 0, maxHp = 1, attack = 0, defense = 0;
		int anchorStacks = 0;
        std::string enemyId;
        float frontalMultiplier = 1.0f, pocketDamageRatio = 0.0f;
        std::vector<float> collisionDamageMultipliers;
        int collisionStage = 0, collisionGraceTicks = 0;
        std::unordered_map<std::uintptr_t, int> collisionGraceByBall;
        bool active = true, defeated = false, pocketed = false, playerSimulation = true;
        int stopCount = 0;
        BossCombatRules::State bossState;
        bool breakBallUsed = false;
        bool nuisanceBall = false;
    };

    struct ContactGuide
    {
        bool hitBall = false;
        bool hitEnemy = false;
        bool chainImpactCenter = false;
        DirectX::SimpleMath::Vector3 playerPosition = DirectX::SimpleMath::Vector3::Zero;
        DirectX::SimpleMath::Vector3 ballPosition = DirectX::SimpleMath::Vector3::Zero;
        DirectX::SimpleMath::Vector3 ballDirection = DirectX::SimpleMath::Vector3::Zero;
    };

    struct Result
    {
        std::vector<DirectX::SimpleMath::Vector3> path;
        // Each new wall/non-piercing ball contact marks an outgoing segment.
        // The full simulation and path remain available to AI callers.
        std::vector<std::size_t> reflectionPathIndices;
        ContactGuide initialContactGuide;
        std::vector<Ball> balls;
		ShotRelicRules shot;
		CushionChargeRules::State cushionCharges{};
		bool cushionBoostConsumed = false;
        bool complete = false, pathTruncated = false;
        int ticks = 0, substeps = 0, damage = 0;
        int bossFixedDamage = 0, breakBallHits = 0;
		int chainImpactHits = 0;
        int stopShieldGranted = 0;
		BallStatus shotStatus{};
		PierceTraceRules::State pierceTraces{};
		HeavyCollisionRules::State heavyCollisions{};
		int heavyCollisionCount = 0;
		int playerAnchorStacks = 0;
		int cushionStrongUses = 0;
		int synergyDamageBonus = 0;
		bool cushionStrongConsumed = false;
		bool heavyFinisherConsumed = false;
		std::unordered_set<std::uintptr_t> uniquePiercedEnemies;
        double milliseconds = 0.0;

        std::size_t PreviewPointCount(std::size_t reflections) const
        {
            return reflectionPathIndices.size() > reflections
                ? (std::min)(path.size(), reflectionPathIndices[reflections] + 1)
                : path.size();
        }
        bool PreviewReachesLimit(std::size_t reflections) const
        {
            return reflectionPathIndices.size() > reflections;
        }
    };

    // Immutable game-owned values plus a stable list of world objects captured
    // immediately before a synchronous prediction. The predictor never reaches
    // back into the Game orchestrator.
    struct Input
    {
        std::vector<BallComponent*> balls;
        std::vector<TableFrame*> tableFrames;
        std::vector<Pocket*> pockets;
        CushionChargeRules::State cushionCharges{};
        PierceTraceRules::State pierceTraces{};
        ShotRelicRules shot{};
        int currentBallCategory = -1;
        float pocketFinisherRatio = 0.0f;
        float pierceSpeedRetention = 1.0f;
        int playerPocketDamage = 0;
        int pierceMaximumUses = 0;
        int relicAttackBonus = 0;
        int heavyCollisionCount = 0;
        int playerAnchorStacks = 0;
        int playerEnemyContacts = 0;
        int enemyEnemyContacts = 0;
        bool hasCurrentBall = false;
        bool hasPierceBallCharger = false;
        bool cushionBoostConsumed = false;
    };

    // Predict through the last physics tick, before healing/respawn/enemy turn.
    // Mouse launches preserve the existing first-tick friction skip; AI launches do not.
    Result Predict(const Input& input, const PlayerBall& player,
        const DirectX::SimpleMath::Vector3& velocity, bool skipFirstPlayerFriction, const PlayerBallData* offer = nullptr);
    std::uint64_t WorldKey(const Input& input);

    // Debug-only, opt-in isolated runtime comparison; never invoked in normal play.
    void WriteVerificationPrediction(const Input& input, const PlayerBall& player,
        const DirectX::SimpleMath::Vector3& velocity, bool skipFirstPlayerFriction);
    void WriteVerificationActual(const Input& input);
}
