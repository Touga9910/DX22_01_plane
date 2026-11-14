#include "Game.h"
#include "GameBattleState.h"
#include "GameRunDefaults.h"
#include "PlayerDeck.h"
#include "RunController.h"
#include "BalanceLogger.h"
#include "AnchorStackRules.h"
#include "PlayerBall.h"
#include "EnemyBall.h"
#include "json/json.hpp"

#include <algorithm>

void Game::NotifyPlayerWallCollision(
	int cushionRegion,
	const DirectX::SimpleMath::Vector3& playerPosition,
	DirectX::SimpleMath::Vector3& reflectedVelocity)
{
    auto rules = CaptureShotRelicRules();
    rules.Wall();
    CommitShotRelicRules(rules);
	const PlayerBallData* currentBall = m_RunController->Deck().GetCurrent();
	if (currentBall == nullptr) return;
	const BallStatus& status = currentBall->status;
	(void)playerPosition;

	(void)cushionRegion;
	CushionChargeRules::ReflectionConfig config;
	config.generatesStacks = currentBall->definitionId == "player_cushion_charge";
	config.generationLimit = status.cushionReflectionLimit;
	config.stacksPerReflection = status.cushionStackGenerateAmount;
	config.consumesStacks = currentBall->definitionId == "player_bounce";
	config.consumptionLimit = status.cushionConsumptionLimit;
	config.reflectedSpeedMultiplier = status.cushionReflectionSpeedMultiplier;
	const int stacksBefore = m_BattleState->cushionCharges.stackCount;
	const int activeSetupBonusBefore =
		m_BattleState->cushionCharges.activeSetupBonus;
	const int uncappedGenerated =
		(config.generatesStacks
			? (std::max)(0, config.stacksPerReflection)
			: 0) +
		(m_BattleState->cushionCharges.setupTriggeredThisShot
			? 0
			: (std::max)(0, activeSetupBonusBefore));
	const auto result = CushionChargeRules::ApplyValidReflection(
		m_BattleState->cushionCharges, config, reflectedVelocity);
	const int stacksAfter = m_BattleState->cushionCharges.stackCount;
	const int stacksAfterGeneration = stacksBefore + result.generated;
	RecordBalanceEvent("cushion_reflection_resolved", {
		{ "cushion_region", cushionRegion },
		{ "position_xz", { playerPosition.x, playerPosition.z } },
		{ "valid_reflections_this_shot",
			m_BattleState->cushionCharges.validReflectionsThisShot },
		{ "generated", result.generated },
		{ "uncapped_generated", uncappedGenerated },
		{ "consumed", result.consumed },
		{ "stacks_before", stacksBefore },
		{ "stacks_after", m_BattleState->cushionCharges.stackCount },
		{ "setup_triggered", result.setupTriggered },
		{ "generation_limit", config.generationLimit },
		{ "consumption_limit", config.consumptionLimit },
		{ "reflected_speed_multiplier", config.reflectedSpeedMultiplier },
		{ "velocity_after_effects_xz",
			{ reflectedVelocity.x, reflectedVelocity.z } },
	});
	if (result.generated > 0 || result.consumed > 0 || result.setupTriggered)
	{
		DirectX::SimpleMath::Vector3 reflectionDirection = reflectedVelocity;
		reflectionDirection.y = 0.0f;
		if (reflectionDirection.LengthSquared() > 0.0001f)
			reflectionDirection.Normalize();
		PublishGameEvent(AxisAbilityResolvedEvent{
			AxisResourceKind::Reflection,
			AxisAbilityAction::ReflectionWallBounce,
			playerPosition,
			playerPosition + reflectionDirection * 8.0f,
			result.generated > 0 ? result.generated : result.consumed,
			m_BattleState->cushionCharges.validReflectionsThisShot,
			0.0f,
			result.consumed > 0 || result.setupTriggered,
			false,
			0,
			0,
			currentBall->definitionId,
		});
	}
	if (result.generated > 0)
	{
		RecordBalanceEvent("cushion_stacks_generated", {
			{ "amount", result.generated },
			{ "setup", result.setupTriggered } });
		NotifyAxisResourceChanged(
			AxisResourceKind::Reflection,
			AxisResourceOwner::Player,
			stacksBefore,
			stacksAfterGeneration,
			AxisResourceChangeReason::WallReflection,
			currentBall->definitionId,
			false,
			playerPosition);
		if (result.setupTriggered &&
			m_BattleState->crossAxisSupportGeneratedThisTurn <
				GameBattleState::CrossAxisSupportLimitPerTurn)
		{
			bool deckHasHeavy = false;
			for (int index = 0;
				index < m_RunController->Deck().GetRewardTargetCount(); ++index)
			{
				const PlayerBallData* ball =
					m_RunController->Deck().GetRewardTarget(index);
				if (ball != nullptr && ball->category == BallCategory::Heavy)
				{
					deckHasHeavy = true;
					break;
				}
			}
			if (deckHasHeavy)
			{
				const auto gain = HeavyCollisionRules::ApplyGain(
					m_BattleState->heavyCollisions, 1);
				if (gain.granted > 0)
				{
					++m_BattleState->crossAxisSupportGeneratedThisTurn;
					RecordBalanceEvent("support_cross_axis_generated", {
						{ "source", "player_reflection_setup" },
						{ "target_axis", "heavy" }, { "amount", gain.granted },
					});
				}
			}
		}
	}
	if (result.consumed > 0)
	{
		m_BattleState->cushionBoostConsumedThisShot = true;
		m_BattleState->cushionStrongUsesThisShot =
			m_BattleState->cushionCharges.consumedThisShot;
		RecordBalanceEvent("cushion_stacks_consumed", {
			{ "amount", result.consumed },
			{ "strong_uses_this_shot", m_BattleState->cushionStrongUsesThisShot },
		});
		NotifyAxisResourceChanged(
			AxisResourceKind::Reflection,
			AxisResourceOwner::Player,
			stacksAfterGeneration,
			stacksAfter,
			AxisResourceChangeReason::AbilityCost,
			currentBall->definitionId,
			false,
			playerPosition);
	}
}

int Game::NotifyEnemyEnemySynergyCollision(
	EnemyBall* first,
	EnemyBall* second,
	const DirectX::SimpleMath::Vector3& firstVelocityBefore,
	const DirectX::SimpleMath::Vector3& secondVelocityBefore)
{
	if (first == nullptr || second == nullptr ||
		!first->CanParticipateInCombat() ||
		!second->CanParticipateInCombat()) return 0;
	const int anchorMaximum = m_RunDefaults->PlayerBallStatus().anchorStackMax;
	const auto anchorShare = AnchorStackRules::ShareEnemyStacks(
		first->MutableAnchorStacks(),
		second->MutableAnchorStacks(),
		anchorMaximum);
	if (anchorShare.firstGained > 0 || anchorShare.secondGained > 0)
	{
		RecordBalanceEvent("anchor_stacks_propagated", {
			{ "first_before", anchorShare.firstBefore },
			{ "second_before", anchorShare.secondBefore },
			{ "shared", anchorShare.shared },
			{ "first_gained", anchorShare.firstGained },
			{ "second_gained", anchorShare.secondGained },
		});
		NotifyAxisResourceChanged(
			AxisResourceKind::Anchor,
			AxisResourceOwner::Enemy,
			anchorShare.firstBefore,
			anchorShare.shared,
			AxisResourceChangeReason::Collision,
			"enemy_anchor_propagation",
			false,
			first->GetPosition(),
			first);
		NotifyAxisResourceChanged(
			AxisResourceKind::Anchor,
			AxisResourceOwner::Enemy,
			anchorShare.secondBefore,
			anchorShare.shared,
			AxisResourceChangeReason::Collision,
			"enemy_anchor_propagation",
			false,
			second->GetPosition(),
			second);
	}
	const std::uintptr_t firstId = reinterpret_cast<std::uintptr_t>(first);
	const std::uintptr_t secondId = reinterpret_cast<std::uintptr_t>(second);
	EnemyBall* target = nullptr;
	const DirectX::SimpleMath::Vector3* velocityBefore = nullptr;
	if (CushionChargeRules::IsPinballTarget(
		m_BattleState->cushionCharges, firstId))
	{
		target = first;
		velocityBefore = &firstVelocityBefore;
	}
	else if (CushionChargeRules::IsPinballTarget(
		m_BattleState->cushionCharges, secondId))
	{
		target = second;
		velocityBefore = &secondVelocityBefore;
	}
	if (target == nullptr || velocityBefore == nullptr ||
		target->GetBall() == nullptr)
	{
		return 0;
	}
	const int damageStage = CushionChargeRules::RegisterPinballCollision(
		m_BattleState->cushionCharges,
		reinterpret_cast<std::uintptr_t>(target));
	CushionChargeRules::PreservePinballSpeed(
		m_BattleState->cushionCharges,
		reinterpret_cast<std::uintptr_t>(target),
		*velocityBefore,
		target->GetBall()->GetMutableVelocity());
	EnemyBall* source = target == first ? second : first;
	PublishGameEvent(AxisAbilityResolvedEvent{
		AxisResourceKind::Reflection,
		AxisAbilityAction::ReflectionPinballCollision,
		source != nullptr ? source->GetPosition() : target->GetPosition(),
		target->GetPosition(),
		damageStage,
		m_BattleState->cushionCharges.pinballCollisionCount,
		0.0f,
		damageStage > 0,
		true,
		reinterpret_cast<std::uintptr_t>(source),
		reinterpret_cast<std::uintptr_t>(target),
		"player_ricochet_finisher",
	});
	return damageStage;
}

void Game::NotifyEnemyWallCollision(
	EnemyBall* enemy,
	const DirectX::SimpleMath::Vector3& velocityBefore,
	DirectX::SimpleMath::Vector3& velocityAfter)
{
	if (enemy == nullptr || !enemy->CanParticipateInCombat()) return;
	const std::uintptr_t enemyId = reinterpret_cast<std::uintptr_t>(enemy);
	if (!CushionChargeRules::IsPinballTarget(
		m_BattleState->cushionCharges, enemyId))
	{
		return;
	}
	CushionChargeRules::RegisterPinballCollision(
		m_BattleState->cushionCharges, enemyId);
	CushionChargeRules::PreservePinballSpeed(
		m_BattleState->cushionCharges,
		enemyId,
		velocityBefore,
		velocityAfter);
	DirectX::SimpleMath::Vector3 direction = velocityAfter;
	direction.y = 0.0f;
	if (direction.LengthSquared() > 0.0001f) direction.Normalize();
	PublishGameEvent(AxisAbilityResolvedEvent{
		AxisResourceKind::Reflection,
		AxisAbilityAction::ReflectionWallBounce,
		enemy->GetPosition(),
		enemy->GetPosition() + direction * 6.0f,
		0,
		m_BattleState->cushionCharges.pinballCollisionCount,
		0.0f,
		true,
		true,
		enemyId,
		0,
		"player_ricochet_finisher",
	});
}

int Game::ConsumeBankShotDamageMultiplier()
{
    auto rules = CaptureShotRelicRules();
    const int multiplier = rules.ConsumeBankShotDamageMultiplier();
    CommitShotRelicRules(rules);
    if (multiplier > 1) RecordBalanceEvent("bank_shot_triggered", {{ "damage_multiplier", multiplier }});
    return multiplier;
}

