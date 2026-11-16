#include "Game.h"
#include "GameDebugController.h"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "GameObject.h"
#include "EnemyBall.h"
#include "EnemyAttackComponent.h"
#include "PlayerBall.h"

#pragma execution_character_set("utf-8")

#include <algorithm>
#include <utility>

namespace
{
	bool CanEnemyAttackThisTurn(const EnemyBall* enemy)
	{
		if (enemy == nullptr ||
			enemy->IsDefeated() ||
			enemy->IsPocketed() ||
			enemy->IsStunned())
		{
			return false;
		}

		const EnemyActionType action = enemy->GetPlannedAction();
		if (action != EnemyActionType::NormalAttack &&
			action != EnemyActionType::ChargeAttack)
		{
			return false;
		}
		if (action == EnemyActionType::ChargeAttack &&
			!enemy->IsCharging())
		{
			return false;
		}

		GameObject* owner = enemy->GetGameObject();
		return owner != nullptr &&
			owner->GetComponent<EnemyAttackComponent>() != nullptr;
	}

	const char* EnemyPlanDebugLabel(const EnemyBall& enemy)
	{
		switch (enemy.GetPlannedAction())
		{
		case EnemyActionType::NormalAttack: return "通常攻撃予定";
		case EnemyActionType::ChargeStart: return "Charge開始予定";
		case EnemyActionType::ChargeAttack: return "Charge攻撃予定";
		default: return "待機";
		}
	}
}

void GameDebugController::InvalidateCombatForecast(const char* reason)
{
	m_DebugCombatForecastDirty = true;
	if (reason != nullptr && reason[0] != '\0')
	{
		m_DebugCombatForecastPendingReason = reason;
	}
}

void GameDebugController::RefreshCombatForecast(Game& game)
{
	if (!m_DebugCombatForecastDirty)
	{
		return;
	}

	CombatForecastSnapshot next{};
	next.updateRevision = m_DebugCombatForecast.updateRevision + 1;
	next.updateReason = m_DebugCombatForecastPendingReason;

	const std::vector<PlayerBall*> players =
		GameRuntime::World().GetComponents<PlayerBall>();
	const std::vector<EnemyBall*> enemies =
		GameRuntime::World().GetComponents<EnemyBall>();
	const PlayerBall* player = players.empty() ? nullptr : players.front();
	if (player != nullptr)
	{
		next.hasPlayer = true;
		next.playerCurrentHp = player->GetHP();
		next.playerMaxHp = player->GetMaxHP();
		next.playerShield = game.GetPlayerShield();
		next.hpAfterAttack = (std::max)(0, player->GetHP());
	}
	int remainingShield = next.playerShield;

	next.enemies.reserve(enemies.size());
	for (const EnemyBall* enemy : enemies)
	{
		if (enemy == nullptr)
		{
			continue;
		}

		EnemyCombatSnapshot enemySnapshot{};
		enemySnapshot.id = enemy->GetEnemyId();
		enemySnapshot.currentHp = enemy->GetHP();
		enemySnapshot.maxHp = enemy->GetMaxHP();
		enemySnapshot.attack = enemy->GetAttack();
		const bool canAttack = CanEnemyAttackThisTurn(enemy);
		enemySnapshot.defeated = enemy->IsDefeated();
		enemySnapshot.pocketed = enemy->IsPocketed();
		enemySnapshot.canAttack = canAttack;
		enemySnapshot.state = enemy->IsDefeated()
			? "撃破"
			: enemy->IsPocketed()
				? "ポケット中"
				: EnemyPlanDebugLabel(*enemy);

		if (canAttack && player != nullptr && !player->IsDefeated())
		{
			const GameObject* owner = enemy->GetGameObject();
			const EnemyAttackComponent* attack = owner != nullptr
				? owner->GetComponent<EnemyAttackComponent>()
				: nullptr;
			next.attackerCount++;
			enemySnapshot.damageBeforeMinimum = attack != nullptr
				? attack->CalculateDamage(
					enemy->GetPlannedDamageMultiplier())
				: 0;
			enemySnapshot.expectedDamage =
				PlayerBall::CalculateIncomingDamage(
					enemySnapshot.damageBeforeMinimum);
			enemySnapshot.minimumDamageApplied =
				enemySnapshot.damageBeforeMinimum < 1;
			next.theoreticalDamage += enemySnapshot.expectedDamage;
			const int absorbed = (std::min)(
				remainingShield,
				enemySnapshot.expectedDamage);
			remainingShield -= absorbed;
			const int appliedDamage = (std::min)(
				next.hpAfterAttack,
				enemySnapshot.expectedDamage - absorbed);
			next.expectedDamage += appliedDamage;
			next.hpAfterAttack -= appliedDamage;
		}

		next.enemies.push_back(std::move(enemySnapshot));
	}

	next.overkillDamage = (std::max)(
		0,
		next.theoreticalDamage - next.playerCurrentHp);
	next.lethal = next.hasPlayer &&
		next.playerCurrentHp > 0 &&
		next.hpAfterAttack <= 0;
	m_DebugCombatForecast = std::move(next);
	m_DebugCombatForecastDirty = false;
}

void GameDebugController::RecordPlayerDamage(
	const std::string& source,
	const std::string& sourceId,
	int damage,
	int hpBefore,
	int hpAfter)
{
	if (damage <= 0)
	{
		return;
	}

	PlayerDamageRecord record{};
	record.sequence = ++m_DebugDamageSequence;
	record.source = source;
	record.sourceId = sourceId;
	record.damage = damage;
	record.hpBefore = hpBefore;
	record.hpAfter = hpAfter;
	m_DebugPlayerDamageHistory.push_front(std::move(record));
	constexpr std::size_t kMaximumHistory = 10;
	while (m_DebugPlayerDamageHistory.size() > kMaximumHistory)
	{
		m_DebugPlayerDamageHistory.pop_back();
	}
}

int GameDebugController::BeginEnemyAttackForecast(Game& game)
{
	InvalidateCombatForecast("敵攻撃開始");
	RefreshCombatForecast(game);
	return m_DebugCombatForecast.expectedDamage;
}

void GameDebugController::EndEnemyAttackForecast(
	int predictedDamage,
	int actualDamage)
{
	m_DebugLastEnemyAttackComparisonValid = true;
	m_DebugLastEnemyAttackPredictedDamage = predictedDamage;
	m_DebugLastEnemyAttackActualDamage = actualDamage;
}

void GameDebugController::ResetDiagnostics(const char* reason)
{
	m_DebugPlayerDamageHistory.clear();
	m_DebugDamageSequence = 0;
	m_DebugLastEnemyAttackComparisonValid = false;
	InvalidateCombatForecast(reason);
}

void Game::InvalidateDebugCombatForecast(const char* reason)
{
	m_DebugController->InvalidateCombatForecast(reason);
}

void Game::RecordDebugPlayerDamage(
	const std::string& source,
	const std::string& sourceId,
	int damage,
	int hpBefore,
	int hpAfter)
{
	m_DebugController->RecordPlayerDamage(
		source,
		sourceId,
		damage,
		hpBefore,
		hpAfter);
}
