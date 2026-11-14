#include "Game.h"
#include "GameBattleState.h"
#include "GameObject.h"
#include "BallComponent.h"
#include "PlayerDeck.h"
#include "RunController.h"
#include "BallMechanics.h"
#include "BalanceLogger.h"
#include "PlayerBall.h"
#include "EnemyBall.h"
#include "PierceMinion.h"
#include "json/json.hpp"

#include <algorithm>

void Game::NotifyPlayerPiercedEnemy(
	EnemyBall* enemy,
	const DirectX::SimpleMath::Vector3& playerPosition,
	const DirectX::SimpleMath::Vector3& direction)
{
	if (enemy == nullptr || !enemy->CanParticipateInCombat()) return;
	const PlayerBallData* currentBall = m_RunController->Deck().GetCurrent();
	if (currentBall == nullptr || currentBall->category != BallCategory::Pierce) return;
	m_BattleState->piercedEnemiesThisShot.insert(enemy);
	const int stacksBefore = m_BattleState->pierceTraces.stackCount;
	const bool uniquePierce = PierceTraceRules::RecordPierce(
		m_BattleState->pierceTraces,
		reinterpret_cast<std::uintptr_t>(enemy),
		1 + currentBall->status.eventStackGenerationBonus);
	const int stackGranted = (std::max)(
		0,
		m_BattleState->pierceTraces.stackCount - stacksBefore);
	DirectX::SimpleMath::Vector3 traceDirection = direction;
	traceDirection.y = 0.0f;
	if (traceDirection.LengthSquared() > 0.0001f)
	{
		traceDirection.Normalize();
		const int overwrittenBefore = m_BattleState->pierceTraces.overwrittenCount;
		const bool traceCreated = PierceTraceRules::AddTrace(
			m_BattleState->pierceTraces,
			playerPosition,
			playerPosition + traceDirection * currentBall->status.traceUseDistance);
		const DirectX::SimpleMath::Vector3 traceEnd = playerPosition +
			traceDirection * currentBall->status.traceUseDistance;
		RecordBalanceEvent("pierce_trace_generated", {
			{ "overwrote_oldest", m_BattleState->pierceTraces.overwrittenCount > overwrittenBefore },
			{ "unique_pierce", uniquePierce },
			{ "requested_amount", uniquePierce
				? 1 + currentBall->status.eventStackGenerationBonus : 0 },
			{ "granted_amount", stackGranted },
			{ "discarded_by_stack_max", uniquePierce
				? 1 + currentBall->status.eventStackGenerationBonus - stackGranted : 0 },
			{ "stack_generated", stackGranted > 0 },
			{ "stacks_before", stacksBefore },
			{ "stacks", m_BattleState->pierceTraces.stackCount },
			{ "trace_count", static_cast<int>(m_BattleState->pierceTraces.traces.size()) },
			{ "trace_start_xz", { playerPosition.x, playerPosition.z } },
			{ "trace_direction_xz", { traceDirection.x, traceDirection.z } },
			{ "trace_length", currentBall->status.traceUseDistance },
		});
		NotifyAxisResourceChanged(
			AxisResourceKind::Pierce,
			AxisResourceOwner::Player,
			stacksBefore,
			m_BattleState->pierceTraces.stackCount,
			AxisResourceChangeReason::PierceHit,
			currentBall->definitionId,
			false,
			enemy->GetPosition(),
			enemy);
		if (traceCreated)
		{
			PublishGameEvent(AxisAbilityResolvedEvent{
				AxisResourceKind::Pierce,
				AxisAbilityAction::PierceTraceCreated,
				playerPosition,
				traceEnd,
				stackGranted,
				static_cast<int>(m_BattleState->pierceTraces.traces.size()),
				0.0f,
				uniquePierce,
				false,
				0,
				reinterpret_cast<std::uintptr_t>(enemy),
				currentBall->definitionId,
			});
		}
	}
}

void Game::NotifyPlayerDirectionChange(
	const DirectX::SimpleMath::Vector3& playerPosition)
{
	(void)playerPosition;
}

void Game::NotifyTraceMovement(
	BallComponent& ball,
	const DirectX::SimpleMath::Vector3& from,
	const DirectX::SimpleMath::Vector3& to)
{
	if (m_BattleState->controller.GetState() != BattleState::BallsMoving) return;
	GameObject* owner = ball.GetGameObject();
	if (owner == nullptr) return;
	DirectX::SimpleMath::Vector3 movement = to - from;
	movement.y = 0.0f;
	const float distance = movement.Length();
	if (PierceMinion* minion = owner->GetComponent<PierceMinion>())
	{
		BalanceLogger::GetInstance().RecordPierceMinionMovement(
			reinterpret_cast<std::uintptr_t>(minion),
			distance);
		if (distance > 0.01f)
		{
			PublishGameEvent(AxisAbilityResolvedEvent{
				AxisResourceKind::Pierce,
				AxisAbilityAction::PierceMinionTravel,
				from,
				to,
				0,
				0,
				0.0f,
				false,
				false,
				reinterpret_cast<std::uintptr_t>(minion),
				0,
				"pierce_minion",
			});
		}
		return;
	}
	EnemyBall* enemy = owner->GetComponent<EnemyBall>();
	if (enemy == nullptr) return;
	BalanceLogger::GetInstance().RecordEnemyMovement(
		reinterpret_cast<std::uintptr_t>(enemy),
		enemy->GetEnemyId(),
		distance);
	if (!enemy->CanParticipateInCombat()) return;
	const PlayerBallData* currentBall = m_RunController->Deck().GetCurrent();
	if (currentBall == nullptr) return;
	HeavyCollisionRules::RecordEnemyMovement(
		m_BattleState->heavyCollisions,
		reinterpret_cast<std::uintptr_t>(enemy),
		distance,
		currentBall->status.abilities.heavyDistanceStacks);
}

int Game::GetPierceMaximumUses() const
{
	const PlayerBallData* ball = m_RunController->Deck().GetCurrent();
	if (ball == nullptr)
		ball = m_RunController->Deck().GetOffer(GetSelectedBallOfferIndex());
	if (ball == nullptr) return 0;
	const int base = BallMechanics::PierceUses(ball->status,
		HasRelic(RelicType::PierceBallCharger) && ball->definitionId == "player_pierce");
	return base;
}

// Pierce Speed Retentionを取得する。

float Game::GetPierceSpeedRetention() const
{
	const PlayerBallData* ball = m_RunController->Deck().GetCurrent();
	if (ball == nullptr)
		ball = m_RunController->Deck().GetOffer(GetSelectedBallOfferIndex());
	if (ball == nullptr) return 0.75f;
	const float base = BallMechanics::PierceRetention(ball->status,
		HasRelic(RelicType::PierceBallCharger) && ball->definitionId == "player_pierce");
	return std::clamp(base, 0.0f, 1.0f);
}

