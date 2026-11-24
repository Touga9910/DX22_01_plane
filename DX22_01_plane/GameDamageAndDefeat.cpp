#include "Game.h"
#include "PlayerShieldRules.h"
#include "GameRuntime.h"
#include "GameRuntimeState.h"
#include "GameWorld.h"
#include "GameBattleState.h"
#include "GamePersistenceRuntime.h"
#include "GameRunDefaults.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "RunController.h"
#include "PlayerBall.h"
#include "EnemyBall.h"
#include "json/json.hpp"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr int kBountyRewardMoney = 5;
}
/// <summary>
/// 受け取ったシールド量を上限付きで反映し、増加した場合はログと演出を更新する。
/// </summary>
void Game::GrantPlayerShield(int amount, const char* source)
{
    const int before = m_BattleState->playerShield;
    m_BattleState->playerShield = std::clamp(before + (std::max)(0, amount), 0, 100000);
    const int gained = m_BattleState->playerShield - before;
    if (gained <= 0) return;
    InvalidateDebugCombatForecast("シールド獲得");
    RecordBalanceEvent("player_shield_gained", {
        { "source", source }, { "amount", gained },
        { "shield", m_BattleState->playerShield } });
    const auto players = GameRuntime::World().GetComponents<PlayerBall>();
    const PlayerBall* player = players.empty() ? nullptr : players.front();
    const auto position = player != nullptr ? player->GetPosition()
        : DirectX::SimpleMath::Vector3::Zero;
    PublishGameEvent(AxisAbilityResolvedEvent{
        AxisResourceKind::Anchor, AxisAbilityAction::AnchorShieldGranted,
        position, position, gained, m_BattleState->playerShield, 0.0f,
        false, false, reinterpret_cast<std::uintptr_t>(player),
        reinterpret_cast<std::uintptr_t>(player), source });
}

/// <summary>
/// シールドから吸収した量を差し引き、HPに届くダメージを返す。
/// </summary>
int Game::AbsorbPlayerShieldDamage(int damage)
{
	const int incoming = (std::max)(0, damage);
	const int hpDamage = PlayerShieldRules::Absorb(m_BattleState->playerShield, incoming);
	const int absorbed = incoming - hpDamage;
	if (absorbed > 0)
	{
		InvalidateDebugCombatForecast("停止時シールド消費");
		RecordBalanceEvent(
			"stop_shield_absorbed",
			{ { "absorbed", absorbed }, { "remaining", m_BattleState->playerShield } });
		const auto players = GameRuntime::World().GetComponents<PlayerBall>();
		const PlayerBall* player = players.empty() ? nullptr : players.front();
		const DirectX::SimpleMath::Vector3 position = player != nullptr
			? player->GetPosition()
			: DirectX::SimpleMath::Vector3::Zero;
		PublishGameEvent(AxisAbilityResolvedEvent{
			AxisResourceKind::Anchor,
			AxisAbilityAction::AnchorShieldAbsorbed,
			position,
			position,
			absorbed,
			m_BattleState->playerShield,
			0.0f,
			absorbed == incoming,
			false,
			reinterpret_cast<std::uintptr_t>(player),
			reinterpret_cast<std::uintptr_t>(player),
			"player_stop_shield",
		});
	}
	return hpDamage;
}

/// <summary>
/// 蓄積した重衝突効果に応じて敵球同士の衝突ダメージを補正する。
/// </summary>
int Game::ScaleEnemyCollisionDamage(int damage) const
{
	return HeavyCollisionRules::ScaleEnemyCollisionDamage(
		m_BattleState->heavyCollisions,
		damage);
}

/// <summary>
/// 戦闘に参加中の直接衝突対象を起点に、周囲への連鎖攻撃を処理する。
/// </summary>
void Game::NotifyPlayerChainImpact(
	EnemyBall* directTarget,
	int attackDamage,
	float radius)
{
	if (directTarget == nullptr)
	{
		return;
	}
	if (!directTarget->CanParticipateInCombat()) return;
	NotifyPlayerChainImpact(
		directTarget->GetPosition(), directTarget, attackDamage, radius);
}

/// <summary>
/// 指定位置から範囲内の敵へ連鎖攻撃を適用し、対応する球固有効果も処理する。
/// </summary>
void Game::NotifyPlayerChainImpact(
	const DirectX::SimpleMath::Vector3& center,
	const EnemyBall* excludedTarget,
	int attackDamage,
	float radius)
{
	const PlayerBallData* currentBall = m_RunController->Deck().GetCurrent();
	if (currentBall != nullptr &&
		currentBall->definitionId == "player_chain_impact" &&
		!m_BattleState->heavyFinisherConsumedThisShot)
	{
		m_BattleState->heavyFinisherConsumedThisShot = true;
		const int stacksBefore =
			m_BattleState->heavyCollisions.stackCount;
		const auto finisher = HeavyCollisionRules::ConsumeForFinisher(
			m_BattleState->heavyCollisions,
			currentBall->status.heavyFinisherDamagePerCollision,
			currentBall->status.heavyCollisionConsumeAmount);
		attackDamage += finisher.bonusDamage;
		RecordBalanceEvent("heavy_finisher_triggered", {
			{ "referenced_collision_count", finisher.referenced },
			{ "consumed_collision_count", finisher.consumed },
			{ "damage_bonus", finisher.bonusDamage },
			{ "remaining_collision_count", m_BattleState->heavyCollisions.stackCount },
		});
		NotifyAxisResourceChanged(
			AxisResourceKind::Heavy,
			AxisResourceOwner::Player,
			stacksBefore,
			m_BattleState->heavyCollisions.stackCount,
			AxisResourceChangeReason::Finisher,
			currentBall->definitionId,
			true,
			center,
			excludedTarget);
	}
	if (attackDamage <= 0 || radius <= 0.0f) return;
	const float radiusSquared = radius * radius;
	struct ChainTarget
	{
		EnemyBall* enemy = nullptr;
		float distanceSquared = 0.0f;
		std::size_t stableOrder = 0;
	};
	std::vector<ChainTarget> targets;
	const auto enemies = GameRuntime::World().GetComponents<EnemyBall>();
	for (std::size_t index = 0; index < enemies.size(); ++index)
	{
		EnemyBall* enemy = enemies[index];
		if (enemy == nullptr || enemy == excludedTarget || enemy->IsDefeated() ||
			enemy->IsPocketed())
		{
			continue;
		}
		DirectX::SimpleMath::Vector3 offset = enemy->GetPosition() - center;
		offset.y = 0.0f;
		const float distanceSquared = offset.LengthSquared();
		if (distanceSquared <= radiusSquared)
		{
			targets.push_back({ enemy, distanceSquared, index });
		}
	}
	std::sort(
		targets.begin(),
		targets.end(),
		[](const ChainTarget& left, const ChainTarget& right)
		{
			if (left.distanceSquared != right.distanceSquared)
				return left.distanceSquared < right.distanceSquared;
			return left.stableOrder < right.stableOrder;
		});
	if (targets.size() > 2) targets.resize(2);
	int hitCount = 0;
	int totalDamage = 0;
	for (const ChainTarget& target : targets)
	{
		EnemyBall* enemy = target.enemy;
		const int hpBefore = enemy->GetHP();
		const DirectX::SimpleMath::Vector3 feedbackPosition = enemy->GetPosition();
		enemy->TakeDamage(attackDamage);
		const int applied = (std::max)(0, hpBefore - enemy->GetHP());
		if (applied <= 0 && !enemy->IsDefeated())
		{
			continue;
		}
		++hitCount;
		totalDamage += applied;
		NotifyCombatFeedback(
			feedbackPosition,
			applied,
			enemy->IsDefeated(),
			true,
			enemy);
	}
	if (hitCount > 0)
	{
		RecordBalanceEvent(
			"chain_impact_triggered",
			{ { "hit_count", hitCount }, { "damage", totalDamage },
			  { "radius", radius }, { "attack_damage", attackDamage } });
	}
	PublishGameEvent(ChainImpactEvent{ center, radius, hitCount });
}

// Player Enemy Relic Damage Bonusを消費する。

/// <summary>
/// 現在のショットでプレイヤー球と敵球の衝突に適用するレリックの加算値を消費する。
/// </summary>
int Game::ConsumePlayerEnemyRelicDamageBonus()
{
    auto rules = CaptureShotRelicRules();
    const int bonus = rules.ConsumePlayerEnemyRelicDamageBonus();
    CommitShotRelicRules(rules);
    return bonus;
}

// Anchor Stoppedを通知する。

/// <summary>
/// アンカーによる停止を戦闘ルールに記録し、直接衝突した対象を保存する。
/// </summary>
void Game::NotifyAnchorStopped(EnemyBall* directTarget)
{
	if (directTarget != nullptr && !directTarget->CanParticipateInCombat())
	{
		return;
	}
    auto rules = CaptureShotRelicRules();
    rules.Anchor();
    CommitShotRelicRules(rules);
	if (directTarget != nullptr) m_BattleState->anchorContactTarget = directTarget;
}

// Pierce Maximum Usesを取得する。

/// <summary>
/// 敵の撃破を戦闘に反映し、対象と戦闘状況に応じて記録や後続処理を行う。
/// </summary>
void Game::NotifyEnemyDefeated(EnemyBall* enemy)
{
	if (enemy == nullptr) return;
	m_BattleState->controller.NotifyEnemyDefeated();
	const std::string enemyId = enemy->GetEnemyId();
	const auto enemies = m_RuntimeState->World().GetComponents<EnemyBall>();
	const auto found = std::find(enemies.begin(), enemies.end(), enemy);
	const std::string targetId = found != enemies.end()
		? "enemy:" + std::to_string(
			static_cast<std::size_t>(std::distance(enemies.begin(), found)))
		: std::string();
	const EnemyActionType plannedAction = enemy->GetPlannedAction();
	const bool attacksNextTurn =
		plannedAction == EnemyActionType::NormalAttack ||
		plannedAction == EnemyActionType::ChargeAttack;
	const int plannedDamage = attacksNextTurn
		? PlayerBall::CalculateIncomingDamage(
			static_cast<int>(std::lround(
				enemy->GetAttack() * enemy->GetPlannedDamageMultiplier())))
		: 0;
	const int futureChargeDamage =
		!attacksNextTurn &&
		(plannedAction == EnemyActionType::ChargeStart || enemy->IsCharging())
			? PlayerBall::CalculateIncomingDamage(
				static_cast<int>(std::lround(
					enemy->GetAttack() *
					GetEnemyActionConfig().chargeDamageMultiplier)))
			: 0;
	RecordBalanceEvent("enemy_defeated", {
		{ "enemy_id", enemyId },
		{ "target_id", targetId },
		{ "board_ability", ToString(enemy->GetBoardAbilityType()) },
		{ "planned_action", ToString(plannedAction) },
		{ "prevented_next_turn_damage_before_defense", plannedDamage },
		{ "prevented_future_charge_damage_before_defense", futureChargeDamage },
	});
	const int enemyAnchorBefore = enemy->GetAnchorStacks();
	const int playerAnchorBefore =
		m_BattleState->anchorStacks.playerStacks;
	const int automaticConsume = AnchorStackRules::DefeatAutoConsumeAmount(
		enemy->GetAnchorStacks());
	if (automaticConsume > 0)
	{
		const auto consumed = AnchorStackRules::ConsumeEnemyStacks(
			enemy->MutableAnchorStacks(),
			m_BattleState->anchorStacks,
			automaticConsume,
			m_RunDefaults->PlayerBallStatus().anchorStackMax);
		RecordBalanceEvent("defeated_enemy_anchor_converted", {
			{ "enemy_id", enemyId },
			{ "consumed", consumed.consumed },
			{ "player_anchor_generated", consumed.playerStacksGenerated },
		});
		NotifyAxisResourceChanged(
			AxisResourceKind::Anchor,
			AxisResourceOwner::Enemy,
			enemyAnchorBefore,
			enemy->GetAnchorStacks(),
			AxisResourceChangeReason::DefeatConversion,
			enemyId,
			false,
			enemy->GetPosition(),
			enemy);
		NotifyAxisResourceChanged(
			AxisResourceKind::Anchor,
			AxisResourceOwner::Player,
			playerAnchorBefore,
			m_BattleState->anchorStacks.playerStacks,
			AxisResourceChangeReason::DefeatConversion,
			enemyId,
			false,
			enemy->GetPosition());
		const auto players = GameRuntime::World().GetComponents<PlayerBall>();
		const PlayerBall* player = players.empty() ? nullptr : players.front();
		const DirectX::SimpleMath::Vector3 playerPosition = player != nullptr
			? player->GetPosition()
			: enemy->GetPosition();
		PublishGameEvent(AxisAbilityResolvedEvent{
			AxisResourceKind::Anchor,
			AxisAbilityAction::AnchorRecovered,
			enemy->GetPosition(),
			playerPosition,
			consumed.consumed,
			consumed.playerStacksGenerated,
			0.0f,
			consumed.playerStacksGenerated > 0,
			false,
			reinterpret_cast<std::uintptr_t>(enemy),
			reinterpret_cast<std::uintptr_t>(player),
			enemyId,
		});
		if (consumed.playerStacksGenerated > 0)
		{
			PublishGameEvent(AxisAbilityResolvedEvent{
				AxisResourceKind::Anchor,
				AxisAbilityAction::AnchorPlayerEmpowered,
				playerPosition,
				playerPosition,
				consumed.playerStacksGenerated,
				m_BattleState->anchorStacks.playerStacks,
				0.0f,
				true,
				false,
				reinterpret_cast<std::uintptr_t>(player),
				reinterpret_cast<std::uintptr_t>(player),
				enemyId,
			});
		}
	}
	enemy->MutableAnchorStacks() = 0;
	if (!HasRelic(RelicType::BountyList) ||
		!m_BattleState->controller.ClaimBountyReward())
	{
		return;
	}
	const int moneyBefore = m_RunController->Status().money;
	m_RunController->AddMoney(kBountyRewardMoney);
	RecordBalanceEvent(
		"bounty_reward",
		{
			{ "enemy_id", enemyId },
			{ "amount", kBountyRewardMoney },
			{ "money_before", moneyBefore },
			{ "money_after", m_RunController->Status().money },
		});
}

// Bank Shot Damage Multiplierを消費する。

/// <summary>
/// ダメージを伴う衝突を種類別にレリックへ記録し、該当する場合はプレイヤー球の能力を更新する。
/// </summary>
void Game::NotifyDamageBallCollision(
	DamageBallCollisionType collisionType)
{
    auto rules = CaptureShotRelicRules();
    rules.Contact(collisionType == DamageBallCollisionType::PlayerEnemy);
    CommitShotRelicRules(rules);
    if (HasRelic(RelicType::CollisionAttackUp))
        for (PlayerBall* player : GameRuntime::World().GetComponents<PlayerBall>()) ApplyRelicModifiersTo(player);
}

// Combat Feedbackを通知する。

/// <summary>
/// プレイヤーの被ダメージをデバッグ情報に記録し、ダメージイベントを発行する。
/// </summary>
void Game::NotifyPlayerDamage(
	const std::string& source,
	int damage,
	const std::string& sourceId,
	int hpBefore,
	int hpAfter)
{
	if (damage > 0)
	{
		InvalidateDebugCombatForecast("プレイヤー被ダメージ");
		RecordDebugPlayerDamage(
			source,
			sourceId,
			damage,
			hpBefore,
			hpAfter);
	}
	PublishGameEvent(PlayerDamageEvent{ source, damage, sourceId });
}

