#include "Game.h"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "GameBattleState.h"
#include "GamePersistenceRuntime.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "RunController.h"
#include "GameObject.h"
#include "PlayerBallText.h"
#include "GameShotPrediction.h"
#include "PlayerBall.h"
#include "EnemyBall.h"
#include "PierceMinion.h"
#include "json/json.hpp"

#include <algorithm>

namespace
{
    constexpr int kEmergencyRepairContactThreshold = 3;
    constexpr int kEmergencyRepairHealAmount = 1;
}

void Game::ApplyEndOfShotRelicEffects(PlayerBall* player)
{
	for (PierceMinion* minion :
		GameRuntime::World().GetComponents<PierceMinion>())
	{
		if (minion != nullptr) minion->Destroy();
	}
	const PlayerBallData* currentBall = m_RunController->Deck().GetCurrent();
	if (currentBall != nullptr && currentBall->category == BallCategory::Anchor)
		m_BattleState->anchorStacks.nextAnchorGrantBonus = 0;
	const int heavyStacksBeforeEnd =
		m_BattleState->heavyCollisions.stackCount;
	const auto heavyGain = HeavyCollisionRules::EndShot(
		m_BattleState->heavyCollisions,
		currentBall != nullptr &&
			currentBall->status.abilities.heavyDistanceStacks
			? currentBall->status.eventStackGenerationBonus
			: 0);
	if (heavyGain.requested > 0)
	{
		RecordBalanceEvent("heavy_distance_stacks_generated", {
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
		heavyStacksBeforeEnd,
		m_BattleState->heavyCollisions.stackCount,
		AxisResourceChangeReason::TravelDistance,
		currentBall != nullptr ? currentBall->definitionId : std::string(),
		false,
		player != nullptr
			? player->GetPosition()
			: DirectX::SimpleMath::Vector3::Zero);

	m_BattleState->anchorContactTarget = nullptr;
	if (currentBall != nullptr &&
		currentBall->definitionId == "player_origin_anchor" &&
		currentBall->status.nextAnchorGrantBonus > 0)
	{
		m_BattleState->anchorStacks.nextAnchorGrantBonus =
			currentBall->status.nextAnchorGrantBonus;
	}

	if (currentBall != nullptr && currentBall->status.abilities.reflectionSetup)
	{
		CushionChargeRules::ArmSetupForNextShot(
			m_BattleState->cushionCharges,
			currentBall->status.reflectionSetupAmount);
		RecordBalanceEvent("reflection_setup_armed", {
			{ "amount", currentBall->status.reflectionSetupAmount },
			{ "pending",
				m_BattleState->cushionCharges.pendingSetupBonus },
		});
	}
	CushionChargeRules::EndPlayerShot(m_BattleState->cushionCharges);
	m_BattleState->cushionBoostConsumedThisShot = false;
	PublishGameEvent(ShotResolvedEvent{
		m_BattleState->presentationHitCount,
		m_BattleState->presentationDamage,
		m_BattleState->presentationDefeatCount,
		m_BattleState->presentationChargeInterruptions,
		m_BattleState->presentationResourceGained,
		m_BattleState->presentationResourceConsumed,
		m_BattleState->presentationFinisherMask,
	});
	// The first verification snapshot is taken at physics stop. Overwrite it
	// after end-of-shot resource generation so the debug comparison covers the
	// same final Heavy state as the predictor.
	GameShotPrediction::WriteVerificationActual();

	if (player == nullptr ||
		!HasRelic(RelicType::EmergencyRepairKit) ||
		GetCurrentShotBallCollisionCount() <
			kEmergencyRepairContactThreshold)
	{
		return;
	}

	const int hpBefore = player->GetHP();
	const int hpAfter = (std::min)(
		player->GetMaxHP(),
		hpBefore + kEmergencyRepairHealAmount);
	if (hpAfter <= hpBefore)
	{
		return;
	}

	player->SetHP(hpAfter);
	CapturePlayerStatusFrom(player);
	InvalidateDebugCombatForecast("プレイヤーHP回復");
	RecordBalanceEvent(
		"emergency_repair_triggered",
		{
			{ "ball_collision_count", GetCurrentShotBallCollisionCount() },
			{ "heal_amount", hpAfter - hpBefore },
			{ "hp_before", hpBefore },
			{ "hp_after", hpAfter },
		});
}

