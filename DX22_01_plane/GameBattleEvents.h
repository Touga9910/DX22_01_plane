#pragma once

#include "GameTypes.h"

#include <string>

class EnemyBall;
class PlayerBall;

namespace DirectX::SimpleMath
{
    struct Vector3;
}

// Facts emitted by collision and ball components after physics has resolved.
// Game remains responsible for routing them to rules, statistics and feedback.
class GameBattleEvents final
{
public:
    static void PlayerWallCollision(
        int cushionRegion,
        const DirectX::SimpleMath::Vector3& playerPosition,
        DirectX::SimpleMath::Vector3& reflectedVelocity);
    static int EnemyEnemySynergyCollision(
        EnemyBall* first,
        EnemyBall* second,
        const DirectX::SimpleMath::Vector3& firstVelocityBefore,
        const DirectX::SimpleMath::Vector3& secondVelocityBefore);
    static PlayerEnemySynergyResult PlayerEnemySynergyCollision(
		EnemyBall* enemy,
		const DirectX::SimpleMath::Vector3& enemyVelocityBefore);
	static void EnemyWallCollision(
		EnemyBall* enemy,
		const DirectX::SimpleMath::Vector3& velocityBefore,
		DirectX::SimpleMath::Vector3& velocityAfter);
    static void PlayerPiercedEnemy(
        EnemyBall* enemy,
        const DirectX::SimpleMath::Vector3& playerPosition,
		const DirectX::SimpleMath::Vector3& direction);
    static void PlayerDirectionChange(
        const DirectX::SimpleMath::Vector3& playerPosition);
    static void PlayerChainImpact(
        EnemyBall* directTarget,
        int attackDamage,
        float radius);
    static void PlayerChainImpact(
        const DirectX::SimpleMath::Vector3& center,
        const EnemyBall* excludedTarget,
        int attackDamage,
        float radius);
    static void AnchorStopped(EnemyBall* directTarget = nullptr);
    static void DamageBallCollision(DamageBallCollisionType collisionType);
    static void CombatFeedback(
        const DirectX::SimpleMath::Vector3& worldPosition,
        int damage,
        bool defeated,
        bool enemyEnemyCollision);
    static void BalanceAutoFullHpEnemySurvived();
    static void EnemyPocketed(EnemyBall* enemy);
    static void EnemyDefeated(EnemyBall* enemy);
    static void PlayerDefeated();
    static void PlayerShotFired(PlayerBall* player);
    static void PlayerDamage(
        const std::string& source,
        int damage,
        const std::string& enemyId,
        int hpBefore,
        int hpAfter);
    static void PlayerPocketFeedback(
        const DirectX::SimpleMath::Vector3& worldPosition,
        int damage);

    GameBattleEvents() = delete;
};
