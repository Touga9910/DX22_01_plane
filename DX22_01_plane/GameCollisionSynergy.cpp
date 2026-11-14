#include "Game.h"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "GameBattleState.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "RunController.h"
#include "BalanceLogger.h"
#include "PlayerBall.h"
#include "EnemyBall.h"
#include "json/json.hpp"

#include <algorithm>
#include <limits>
PlayerEnemySynergyResult Game::NotifyPlayerEnemySynergyCollision(
	EnemyBall* enemy,
	const DirectX::SimpleMath::Vector3& enemyVelocityBefore)
{
	PlayerEnemySynergyResult result;
	result.damageBonus = m_BattleState->synergyDamageBonusThisShot;
	if (enemy == nullptr || !enemy->CanParticipateInCombat())
	{
		result.damageBonus = 0;
		return result;
	}
	const PlayerBallData* currentBall = m_RunController->Deck().GetCurrent();
	if (currentBall == nullptr) return result;
	const BallStatus& status = currentBall->status;
	if (status.anchorEnemyStackGenerate > 0 &&
		m_BattleState->anchorContactTarget == nullptr)
	{
		// Commit the first valid anchor contact immediately. Shot-end processing
		// can run after the player has entered a pocket and must not own this grant.
		m_BattleState->anchorContactTarget = enemy;
		const int oneShotBonus = currentBall->category == BallCategory::Anchor
			? m_BattleState->anchorStacks.nextAnchorGrantBonus
			: 0;
		const auto grant = AnchorStackRules::GrantEnemyStacksOnCollision(
			enemy->MutableAnchorStacks(),
			status.anchorEnemyStackGenerate,
			m_BattleState->anchorStacks.playerStacks,
			status.anchorPlayerStacksPerBonus,
			oneShotBonus,
			status.anchorStackMax);
		if (grant.granted > 0)
		{
			const auto players = GameRuntime::World().GetComponents<PlayerBall>();
			const PlayerBall* player = players.empty() ? nullptr : players.front();
			const DirectX::SimpleMath::Vector3 sourcePosition = player != nullptr
				? player->GetPosition()
				: enemy->GetPosition();
			RecordBalanceEvent("anchor_stacks_generated", {
				{ "enemy_amount", grant.granted },
				{ "requested_amount", grant.requested },
				{ "player_anchor", m_BattleState->anchorStacks.playerStacks },
				{ "one_shot_bonus", oneShotBonus },
				{ "timing", "collision" },
			});
			PublishGameEvent(AxisAbilityResolvedEvent{
				AxisResourceKind::Anchor,
				AxisAbilityAction::AnchorApplied,
				sourcePosition,
				enemy->GetPosition(),
				grant.granted,
				grant.after,
				0.0f,
				grant.after >= 5,
				false,
				reinterpret_cast<std::uintptr_t>(player),
				reinterpret_cast<std::uintptr_t>(enemy),
				currentBall->definitionId,
			});
		}
		NotifyAxisResourceChanged(
			AxisResourceKind::Anchor,
			AxisResourceOwner::Enemy,
			grant.before,
			grant.after,
			AxisResourceChangeReason::Collision,
			currentBall->definitionId,
			false,
			enemy->GetPosition(),
			enemy);
	}

	if (status.abilities.lightweight)
	{
		enemy->ApplyLightweight(status.lightweightKnockbackMultiplier);
		const int heavyStacksBefore =
			m_BattleState->heavyCollisions.stackCount;
		const auto heavyGain = HeavyCollisionRules::GenerateOncePerEnemy(
			m_BattleState->heavyCollisions,
			reinterpret_cast<std::uintptr_t>(enemy),
			1 + status.eventStackGenerationBonus);
		if (heavyGain.requested > 0)
		{
			RecordBalanceEvent("heavy_micro_stack_generated", {
				{ "amount", heavyGain.granted },
				{ "uncapped_amount", heavyGain.requested },
				{ "granted_amount", heavyGain.granted },
				{ "limited_amount", heavyGain.discardedByTurnLimit },
				{ "storage_limited_amount", heavyGain.discardedByStorageLimit },
				{ "gain_limit", heavyGain.turnLimit },
				{ "turn_gained_after", m_BattleState->heavyCollisions.gainedThisTurn },
				{ "stack_after", m_BattleState->heavyCollisions.stackCount },
			});
		}
		NotifyAxisResourceChanged(
			AxisResourceKind::Heavy,
			AxisResourceOwner::Player,
			heavyStacksBefore,
			m_BattleState->heavyCollisions.stackCount,
			AxisResourceChangeReason::Collision,
			currentBall->definitionId,
			false,
			enemy->GetPosition(),
			enemy);
		if (heavyGain.granted > 0 &&
			m_BattleState->crossAxisSupportGeneratedThisTurn <
				GameBattleState::CrossAxisSupportLimitPerTurn)
		{
			bool deckHasPierce = false;
			for (int index = 0;
				index < m_RunController->Deck().GetRewardTargetCount(); ++index)
			{
				const PlayerBallData* ball =
					m_RunController->Deck().GetRewardTarget(index);
				if (ball != nullptr && ball->category == BallCategory::Pierce)
				{
					deckHasPierce = true;
					break;
				}
			}
			if (deckHasPierce &&
				m_BattleState->pierceTraces.stackCount < PierceTraceRules::MaxStackCount)
			{
				++m_BattleState->pierceTraces.stackCount;
				++m_BattleState->crossAxisSupportGeneratedThisTurn;
				RecordBalanceEvent("support_cross_axis_generated", {
					{ "source", currentBall->definitionId },
					{ "target_axis", "pierce" }, { "amount", 1 },
				});
			}
		}
	}

	if (currentBall->definitionId == "player_bounce" &&
		CushionChargeRules::ConsumeEmpoweredHit(
			m_BattleState->cushionCharges,
			reinterpret_cast<std::uintptr_t>(enemy)))
	{
		result.damageBonus += status.cushionBounceAttackBonus;
		RecordBalanceEvent("cushion_empowered_hit", {
			{ "target_id", reinterpret_cast<std::uintptr_t>(enemy) },
			{ "damage_bonus", status.cushionBounceAttackBonus },
			{ "empowered_hits_remaining",
				m_BattleState->cushionCharges.empoweredHitsRemaining },
		});
		PublishGameEvent(AxisAbilityResolvedEvent{
			AxisResourceKind::Reflection,
			AxisAbilityAction::ReflectionEmpoweredHit,
			enemy->GetPosition(),
			enemy->GetPosition(),
			status.cushionBounceAttackBonus,
			m_BattleState->cushionStrongUsesThisShot,
			0.0f,
			true,
			false,
			0,
			reinterpret_cast<std::uintptr_t>(enemy),
			currentBall->definitionId,
		});
	}
	if (currentBall->definitionId == "player_pierce_finisher" &&
		!m_BattleState->piercedEnemiesThisShot.empty())
	{
		const int uniqueEnemies = static_cast<int>(m_BattleState->piercedEnemiesThisShot.size());
		result.damageBonus += status.pierceFinisherBaseBonus +
			(std::max)(0, uniqueEnemies - 1) * status.pierceFinisherMultiTargetBonus;
		RecordBalanceEvent("pierce_finisher_hit", {
			{ "unique_enemies", uniqueEnemies },
			{ "damage_bonus", result.damageBonus },
		});
		PublishGameEvent(AxisAbilityResolvedEvent{
			AxisResourceKind::Pierce,
			AxisAbilityAction::PierceFinisherHit,
			enemy->GetPosition(),
			enemy->GetPosition(),
			result.damageBonus,
			uniqueEnemies,
			0.0f,
			uniqueEnemies >= 2,
			true,
			0,
			reinterpret_cast<std::uintptr_t>(enemy),
			currentBall->definitionId,
		});
		if (!m_BattleState->pierceFinisherPresentedThisShot)
		{
			m_BattleState->pierceFinisherPresentedThisShot = true;
			NotifyAxisResourceChanged(
				AxisResourceKind::Pierce,
				AxisResourceOwner::Player,
				m_BattleState->pierceTraces.stackCount,
				m_BattleState->pierceTraces.stackCount,
				AxisResourceChangeReason::Finisher,
				currentBall->definitionId,
				true,
				enemy->GetPosition(),
				enemy);
		}
	}

	if (currentBall->definitionId == "player_ricochet_finisher")
	{
		const std::uintptr_t enemyId = reinterpret_cast<std::uintptr_t>(enemy);
		const int stacksBefore = m_BattleState->cushionCharges.stackCount;
		const bool activated = CushionChargeRules::ActivatePinball(
			m_BattleState->cushionCharges,
			enemyId,
			status.ricochetStackConsume,
			status.pinballCollisionsPerStage,
			status.pinballSpeedRetention);
		RecordBalanceEvent("pinball_activation_attempted", {
			{ "activated", activated },
			{ "required", status.ricochetStackConsume },
			{ "consumed", activated ? status.ricochetStackConsume : 0 },
			{ "stacks_before", stacksBefore },
			{ "stacks_after", m_BattleState->cushionCharges.stackCount },
			{ "target_id", enemyId },
			{ "collisions_per_damage_stage",
				status.pinballCollisionsPerStage },
			{ "speed_retention", status.pinballSpeedRetention },
		});
		if (CushionChargeRules::IsPinballTarget(
			m_BattleState->cushionCharges, enemyId))
		{
			CushionChargeRules::RegisterPinballCollision(
				m_BattleState->cushionCharges, enemyId);
			if (enemy->GetBall() != nullptr)
			{
				CushionChargeRules::PreservePinballSpeed(
					m_BattleState->cushionCharges,
					enemyId,
					enemyVelocityBefore,
					enemy->GetBall()->GetMutableVelocity());
			}
		}
		result.stopPlayer = activated;
		if (activated)
		{
			RecordBalanceEvent("pinball_activated", {
				{ "consumed", status.ricochetStackConsume },
				{ "remaining", m_BattleState->cushionCharges.stackCount },
			});
			NotifyAxisResourceChanged(
				AxisResourceKind::Reflection,
				AxisResourceOwner::Player,
				stacksBefore,
				m_BattleState->cushionCharges.stackCount,
				AxisResourceChangeReason::Finisher,
				currentBall->definitionId,
				true,
				enemy->GetPosition(),
				enemy);
			PublishGameEvent(AxisAbilityResolvedEvent{
				AxisResourceKind::Reflection,
				AxisAbilityAction::ReflectionPinballActivated,
				enemy->GetPosition(),
				enemy->GetPosition(),
				status.ricochetStackConsume,
				0,
				0.0f,
				true,
				true,
				0,
				enemyId,
				currentBall->definitionId,
			});
		}
	}

	if (currentBall->definitionId == "player_stop_shield" &&
		enemy->GetAnchorStacks() > 0)
	{
		const int enemyStacksBefore = enemy->GetAnchorStacks();
		const int playerStacksBefore =
			m_BattleState->anchorStacks.playerStacks;
		const auto consumed = AnchorStackRules::ConsumeEnemyStacks(
			enemy->MutableAnchorStacks(),
			m_BattleState->anchorStacks,
			status.anchorEnemyConsumeLimit,
			status.anchorStackMax);
		const int shieldGain = AnchorStackRules::CalculateShield(
			consumed.consumed,
			status.stopShieldAmount);
		m_BattleState->playerShield = (std::min)(
			status.stopShieldAmount,
			m_BattleState->playerShield + shieldGain);
		if (shieldGain > 0 &&
			m_BattleState->crossAxisSupportGeneratedThisTurn <
				GameBattleState::CrossAxisSupportLimitPerTurn)
		{
			bool deckHasReflection = false;
			for (int index = 0;
				index < m_RunController->Deck().GetRewardTargetCount(); ++index)
			{
				const PlayerBallData* ball =
					m_RunController->Deck().GetRewardTarget(index);
				if (ball != nullptr && ball->category == BallCategory::Bounce)
				{
					deckHasReflection = true;
					break;
				}
			}
			if (deckHasReflection)
			{
				++m_BattleState->cushionCharges.stackCount;
				++m_BattleState->crossAxisSupportGeneratedThisTurn;
				RecordBalanceEvent("support_cross_axis_generated", {
					{ "source", currentBall->definitionId },
					{ "target_axis", "reflection" }, { "amount", 1 },
				});
			}
		}
		RecordBalanceEvent("stop_shield_granted", {
			{ "consumed", consumed.consumed },
			{ "shield_gain", shieldGain },
			{ "shield", m_BattleState->playerShield },
			{ "player_anchor_generated", consumed.playerStacksGenerated },
		});
		NotifyAxisResourceChanged(
			AxisResourceKind::Anchor,
			AxisResourceOwner::Enemy,
			enemyStacksBefore,
			enemy->GetAnchorStacks(),
			AxisResourceChangeReason::ShieldConversion,
			currentBall->definitionId,
			false,
			enemy->GetPosition(),
			enemy);
		NotifyAxisResourceChanged(
			AxisResourceKind::Anchor,
			AxisResourceOwner::Player,
			playerStacksBefore,
			m_BattleState->anchorStacks.playerStacks,
			AxisResourceChangeReason::ShieldConversion,
			currentBall->definitionId,
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
			currentBall->definitionId,
		});
		PublishGameEvent(AxisAbilityResolvedEvent{
			AxisResourceKind::Anchor,
			AxisAbilityAction::AnchorShieldGranted,
			playerPosition,
			playerPosition,
			shieldGain,
			m_BattleState->playerShield,
			0.0f,
			shieldGain >= status.stopShieldAmount,
			false,
			reinterpret_cast<std::uintptr_t>(player),
			reinterpret_cast<std::uintptr_t>(player),
			currentBall->definitionId,
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
				currentBall->definitionId,
			});
		}
	}

	if (currentBall->definitionId == "player_anchor_finisher" &&
		!m_BattleState->anchorFinisherTriggeredThisShot &&
		enemy->GetAnchorStacks() > 0)
	{
		m_BattleState->anchorFinisherTriggeredThisShot = true;
		const int playerStacksBefore =
			m_BattleState->anchorStacks.playerStacks;
		const int damagePerStack =
			AnchorStackRules::FinisherDamagePerEnemyStack(playerStacksBefore);
		const int available = enemy->GetAnchorStacks();
		const int requested = status.anchorFinisherStackConsume <= 0
			? available
			: (std::min)(available, status.anchorFinisherStackConsume);
		const int directBonus = requested * damagePerStack;
		result.damageBonus += directBonus;
		const auto consumed = AnchorStackRules::ConsumeEnemyStacks(
			enemy->MutableAnchorStacks(),
			m_BattleState->anchorStacks,
			requested,
			status.anchorStackMax);

		int aoeHitCount = 0;
		const int aoeDamage = directBonus / 2;
		if (consumed.consumed >= status.anchorFinisherAoeThreshold &&
			aoeDamage > 0 && status.anchorFinisherAoeRadius > 0.0f)
		{
			const float radiusSquared = status.anchorFinisherAoeRadius *
				status.anchorFinisherAoeRadius;
			for (EnemyBall* other :
				GameRuntime::World().GetComponents<EnemyBall>())
			{
				if (other == nullptr || other == enemy || other->IsDefeated() ||
					other->IsPocketed())
				{
					continue;
				}
				DirectX::SimpleMath::Vector3 offset =
					other->GetPosition() - enemy->GetPosition();
				offset.y = 0.0f;
				if (offset.LengthSquared() > radiusSquared) continue;
				const int hpBefore = other->GetHP();
				const bool wasDefeated = other->IsDefeated();
				const DirectX::SimpleMath::Vector3 feedbackPosition =
					other->GetPosition();
				other->TakeDamage(aoeDamage);
				const int applied = (std::max)(0, hpBefore - other->GetHP());
				if (!wasDefeated && (applied > 0 || other->IsDefeated()))
				{
					NotifyCombatFeedback(
						feedbackPosition,
						applied,
						other->IsDefeated(),
						false,
						other);
				}
				++aoeHitCount;
			}
		}
		RecordBalanceEvent("anchor_finisher_triggered", {
			{ "consumed", consumed.consumed },
			{ "player_anchor_before", playerStacksBefore },
			{ "damage_per_stack", damagePerStack },
			{ "direct_damage_bonus", directBonus },
			{ "aoe_hit_count", aoeHitCount },
			{ "player_anchor_generated", consumed.playerStacksGenerated },
		});
		NotifyAxisResourceChanged(
			AxisResourceKind::Anchor,
			AxisResourceOwner::Enemy,
			available,
			enemy->GetAnchorStacks(),
			AxisResourceChangeReason::Finisher,
			currentBall->definitionId,
			true,
			enemy->GetPosition(),
			enemy);
		NotifyAxisResourceChanged(
			AxisResourceKind::Anchor,
			AxisResourceOwner::Player,
			playerStacksBefore,
			m_BattleState->anchorStacks.playerStacks,
			AxisResourceChangeReason::Finisher,
			currentBall->definitionId,
			true,
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
			true,
			true,
			reinterpret_cast<std::uintptr_t>(enemy),
			reinterpret_cast<std::uintptr_t>(player),
			currentBall->definitionId,
		});
		PublishGameEvent(AxisAbilityResolvedEvent{
			AxisResourceKind::Anchor,
			AxisAbilityAction::AnchorFinisherArea,
			enemy->GetPosition(),
			enemy->GetPosition(),
			consumed.consumed,
			aoeHitCount,
			status.anchorFinisherAoeRadius,
			true,
			true,
			reinterpret_cast<std::uintptr_t>(enemy),
			reinterpret_cast<std::uintptr_t>(enemy),
			currentBall->definitionId,
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
				true,
				reinterpret_cast<std::uintptr_t>(player),
				reinterpret_cast<std::uintptr_t>(player),
				currentBall->definitionId,
			});
		}
	}

	return result;
}

