#include "Game.h"
#include "GameBattleState.h"
#include "GameRuntimeState.h"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "GameObject.h"
#include "BallComponent.h"
#include "EnemyBall.h"
#include "PlayerBall.h"
#include "RunController.h"
#include "PlayerRunStatus.h"
#include "TableConfig.h"
#include "BossPocketRules.h"
#include "Pocket.h"
#include "json/json.hpp"

#pragma execution_character_set("utf-8")

#include <algorithm>
#include <cmath>
#include <random>

using DirectX::SimpleMath::Vector3;

/// <summary>
/// 戦闘中の敵がすべて撃破済みかを返す。
/// </summary>
bool Game::AreAllEnemiesDefeated() const
{
	return m_BattleState->controller.AreAllEnemiesDefeated();
}

/// <summary>
/// 敵の生存状態とボスの装甲やHPから、ポケットでとどめを刺せるかを判定する。
/// </summary>
bool Game::IsEnemyPocketFinisherEligible(const EnemyBall* enemy) const
{
    return enemy != nullptr && !enemy->IsDefeated() && !enemy->IsPocketed() &&
        (!enemy->IsArmorBoss() || (enemy->GetBossState().IsBroken() &&
            enemy->GetHP() <= BossCombatRules::PocketDamage(enemy->GetMaxHP())));
}

/// <summary>
/// 最大HPと戦闘の設定から、自球がポケットに落ちた際のダメージを求める。
/// </summary>
int Game::GetPlayerPocketDamageAmount() const
{
	return (std::max)(1, static_cast<int>(std::ceil(
		static_cast<float>(m_RunController->Status().maxHp) *
		m_BattleState->controller.PocketRules().playerDamageRatio)));
}

/// <summary>
/// ポケットに落ちた敵を処理し、装甲ボスの復帰や撃破済みの球の除去を行う。
/// </summary>
void Game::HandleEnemyPocket(EnemyBall* enemy)
{
	if (enemy == nullptr || enemy->IsPocketed())
	{
		return;
	}
	const bool newlyDefeated = !enemy->IsDefeated();
	const int hpBefore = enemy->GetHP();
	const int maxHp = enemy->GetMaxHP();
	const Vector3 pocketPosition = enemy->GetPosition();
    if (newlyDefeated && enemy->IsArmorBoss())
    {
        std::vector<BossPocketRules::Obstacle> obstacles;
        for (const auto* ball : GameRuntime::World().GetComponents<BallComponent>())
            if (ball && ball != enemy->GetBall() && ball->GetGameObject() && ball->GetGameObject()->IsActive())
                obstacles.push_back({ball->GetPosition(), ball->GetRadius()});
        std::vector<Collision::Sphere> pockets;
        for (const auto* pocket : GameRuntime::World().GetComponents<Pocket>())
            if (pocket && pocket->GetGameObject() && pocket->GetGameObject()->IsActive())
                pockets.push_back(pocket->GetSphere());
        const auto position = BossPocketRules::FindReturnPosition(pocketPosition, enemy->GetRadius(), obstacles, pockets);
        if (!position)
        {
            enemy->GetBall()->GetMutableVelocity() = Vector3::Zero;
            enemy->GetBall()->GetMutableAcceleration() = Vector3::Zero;
            return;
        }
        const auto result = enemy->ResolveBossPocket();
        if (!enemy->IsDefeated())
        {
            enemy->GetBall()->ResetAtPosition(*position);
            if (result.applied)
            {
                RecordBalanceEvent("boss_pocket_returned", {{"boss_id", enemy->GetEnemyId()},
                    {"position_x", position->x}, {"position_z", position->z}});
                NotifyPocketFeedback(pocketPosition, false, false, result.damage, enemy);
            }
            return;
        }
    }
	if (newlyDefeated)
	{
		enemy->Defeat();
		m_BattleState->controller.NotifyEnemyPocketDefeated();
	}
	if (newlyDefeated)
	{
		NotifyPocketFeedback(pocketPosition, false, true, hpBefore, enemy);
	}
	enemy->RemoveFromFieldAfterPocket();
	RecordBalanceEvent(
		newlyDefeated ? "enemy_pocket_defeat" : "enemy_corpse_pocket_removed",
		{
			{ "enemy_id", enemy->GetEnemyId() },
			{ "stage_type", ToString(m_BattleState->controller.GetStageType()) },
			{ "combat_mode", ToString(
				m_BattleState->controller.GetCombatMode()) },
			{ "hp_before", hpBefore },
			{ "max_hp", maxHp },
			{ "newly_defeated", newlyDefeated },
			{ "cumulative_defeated_count",
				m_BattleState->controller.GetDefeatedEnemyCount() },
			{ "pocket_defeated_count",
				m_BattleState->controller.GetPocketDefeatedEnemyCount() },
		});
	m_RuntimeState->World().RequestDestroy(enemy->GetGameObject());
}

/// <summary>
/// 球やポケットとの干渉を避けながら、自球を盤面に戻す候補位置を探す。
/// </summary>
Vector3 Game::FindPlayerPocketReturnPosition(const PlayerBall* player)
{
	std::uniform_real_distribution<float> xDistribution(
		-m_BattleState->controller.PocketRules().playerReturnHalfWidth,
		m_BattleState->controller.PocketRules().playerReturnHalfWidth);
	std::uniform_real_distribution<float> zDistribution(
		-m_BattleState->controller.PocketRules().playerReturnHalfDepth,
		m_BattleState->controller.PocketRules().playerReturnHalfDepth);
	const float playerRadius = player != nullptr && player->GetBall() != nullptr
		? player->GetBall()->GetRadius()
		: 2.4f;

	for (int attempt = 0; attempt < 24; attempt++)
	{
		const Vector3 candidate(
			xDistribution(m_BattleState->controller.PocketRandomEngine()),
			TableConfig::FIELD_HEIGHT,
			zDistribution(m_BattleState->controller.PocketRandomEngine()));
		bool blocked = false;
		for (BallComponent* ball :
			GameRuntime::World().GetComponents<BallComponent>())
		{
			if (ball == nullptr || ball->GetGameObject() == nullptr ||
				!ball->GetGameObject()->IsActive() ||
				(player != nullptr && ball == player->GetBall()))
			{
				continue;
			}
			Vector3 difference = candidate - ball->GetPosition();
			difference.y = 0.0f;
			const float clearance = playerRadius + ball->GetRadius() + 0.5f;
			if (difference.LengthSquared() < clearance * clearance)
			{
				blocked = true;
				break;
			}
		}
		if (!blocked)
		{
			return candidate;
		}
	}
	return Vector3(0.0f, TableConfig::FIELD_HEIGHT, 0.0f);
}

/// <summary>
/// ポケットに落ちた生存中の自球を盤面へ戻し、復帰位置を記録する。
/// </summary>
void Game::RestorePocketedPlayer()
{
	for (PlayerBall* player :
		GameRuntime::World().GetComponents<PlayerBall>())
	{
		if (player == nullptr || player->IsDefeated() ||
			!player->IsPocketed())
		{
			continue;
		}

		const Vector3 returnPosition =
			FindPlayerPocketReturnPosition(player);
		player->ReturnFromPocket(returnPosition);
		RecordBalanceEvent(
			"player_pocket_returned",
			{
				{ "position_x", returnPosition.x },
				{ "position_z", returnPosition.z },
			});
	}
}
