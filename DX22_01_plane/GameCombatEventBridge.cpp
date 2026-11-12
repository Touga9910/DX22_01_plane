#include "Game.h"
#include "GameRuntimeState.h"
#include "GameWorld.h"
#include "GameBattleState.h"
#include "GameBalanceRuntime.h"
#include "GamePersistenceRuntime.h"
#include "PlayerDeck.h"
#include "RunController.h"
#include "BalanceLogger.h"
#include "GamePresentation.h"
#include "AudioFeedback.h"
#include "PlayerBall.h"
#include "EnemyBall.h"
#include "EnemyAttackComponent.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

void Game::NotifyCombatFeedback(
	const DirectX::SimpleMath::Vector3& worldPosition,
	int damage,
	bool defeated,
	bool enemyEnemyCollision,
	const EnemyBall* target)
{
	InvalidateDebugCombatForecast("エネミー被ダメージ");
	PublishGameEvent(EnemyDamageEvent{
		worldPosition,
		damage,
		defeated,
		enemyEnemyCollision,
		reinterpret_cast<std::uintptr_t>(target),
		target != nullptr ? target->GetEnemyId() : std::string(),
	});
}

// Pocket Feedbackを通知する。

void Game::NotifyCollisionImpact(
	const DirectX::SimpleMath::Vector3& worldPosition,
	const DirectX::SimpleMath::Vector3& firstTargetPosition,
	const DirectX::SimpleMath::Vector3& secondTargetPosition,
	float relativeApproachSpeed,
	CombatCollisionKind collisionKind,
	int damage,
	bool defeated,
	const EnemyBall* firstTarget,
	const EnemyBall* secondTarget)
{
	const PlayerBallData* currentBall = m_RunController->Deck().GetCurrent();
	PublishGameEvent(CollisionImpactEvent{
		worldPosition,
		firstTargetPosition,
		secondTargetPosition,
		relativeApproachSpeed,
		collisionKind,
		damage,
		defeated,
		reinterpret_cast<std::uintptr_t>(firstTarget),
		reinterpret_cast<std::uintptr_t>(secondTarget),
		firstTarget != nullptr ? firstTarget->GetEnemyId() : std::string(),
		secondTarget != nullptr ? secondTarget->GetEnemyId() : std::string(),
		currentBall != nullptr
			? static_cast<int>(currentBall->category)
			: static_cast<int>(BallCategory::Standard),
	});
}

void Game::NotifyWallImpact(
	const DirectX::SimpleMath::Vector3& worldPosition,
	float relativeApproachSpeed,
	const void* target)
{
	PublishGameEvent(WallImpactEvent{
		worldPosition,
		relativeApproachSpeed,
		reinterpret_cast<std::uintptr_t>(target),
	});
}

void Game::NotifyPocketFeedback(
	const DirectX::SimpleMath::Vector3& worldPosition,
	bool playerPocket,
	bool finisher,
	int damage,
	const EnemyBall* target)
{
	InvalidateDebugCombatForecast(
		playerPocket ? "プレイヤーポケット" : "エネミーポケット");
	PublishGameEvent(PocketFeedbackEvent{
		worldPosition,
		playerPocket,
		finisher,
		damage,
		reinterpret_cast<std::uintptr_t>(target),
		target != nullptr ? target->GetEnemyId() : std::string(),
	});
}

// Player Damageを通知する。

void Game::NotifyChargeInterrupted(
	const EnemyBall& enemy,
	const ChargeInterruptionResult& result)
{
	const DirectX::SimpleMath::Vector3 position =
		result.reason == ChargeInterruptReason::Pocketed
			? enemy.GetPocketEntryPosition()
			: enemy.GetPosition();
	InvalidateDebugCombatForecast("Charge妨害");
	const auto enemies = m_RuntimeState->World().GetComponents<EnemyBall>();
	const auto found = std::find(enemies.begin(), enemies.end(), &enemy);
	const std::string targetId = found != enemies.end()
		? "enemy:" + std::to_string(
			static_cast<std::size_t>(std::distance(enemies.begin(), found)))
		: std::string();
	RecordBalanceEvent("enemy_charge_interrupted", {
		{ "enemy_id", enemy.GetEnemyId() },
		{ "target_id", targetId },
		// This callback is emitted only after a currently charging enemy has
		// been interrupted. The EnemyBall has already cleared its internal
		// planned action by this point, so reading it here incorrectly reports
		// Wait and classifies an immediate prevented Charge as a future benefit.
		{ "planned_action", ToString(EnemyActionType::ChargeAttack) },
		{ "prevented_attack_damage",
			static_cast<int>(std::ceil(
				enemy.GetAttack() *
				GetEnemyActionConfig().chargeDamageMultiplier)) },
		{ "reason", ToString(result.reason) },
		{ "movement_distance", result.movementDistance },
		{ "movement_threshold", result.movementThreshold },
		{ "pierce_damage", result.pierceDamage },
		{ "pierce_damage_threshold", result.pierceDamageThreshold },
	});
	PublishGameEvent(ChargeInterruptedEvent{
		position,
		static_cast<int>(result.reason),
		reinterpret_cast<std::uintptr_t>(&enemy),
		enemy.GetEnemyId(),
	});
}

void Game::NotifyAxisResourceChanged(
	AxisResourceKind axis,
	AxisResourceOwner owner,
	int valueBefore,
	int valueAfter,
	AxisResourceChangeReason reason,
	const std::string& abilityId,
	bool finisher,
	const DirectX::SimpleMath::Vector3& worldPosition,
	const EnemyBall* target)
{
	if (valueBefore == valueAfter && !finisher)
	{
		return;
	}
	PublishGameEvent(AxisResourceChangedEvent{
		axis,
		owner,
		valueBefore,
		valueAfter,
		reason,
		abilityId,
		finisher,
		worldPosition,
		reinterpret_cast<std::uintptr_t>(target),
	});
}

// Game Eventを公開する。

void Game::PublishGameEvent(const GameEvent& event)
{
	std::visit(
		[this](const auto& value)
		{
			using EventType = std::decay_t<decltype(value)>;
			if constexpr (std::is_same_v<EventType, ShotFiredEvent>)
			{
				m_PersistenceRuntime->Statistics().BeginShot(value.ballId);
				if (m_AudioFeedback != nullptr)
				{
					m_AudioFeedback->OnShotFired(value);
				}
				if (m_GamePresentation != nullptr)
				{
					m_GamePresentation->OnShotFired(value.ballCategory);
				}
			}
			else if constexpr (std::is_same_v<EventType, CollisionImpactEvent>)
			{
				if (m_GamePresentation != nullptr &&
					!m_BalanceRuntime->autoPlayer.IsEnabled())
				{
					m_GamePresentation->OnCollisionImpact(value);
				}
				if (m_AudioFeedback != nullptr)
				{
					const int existingHeavyChainCount =
						m_GamePresentation != nullptr
						? m_GamePresentation->GetHeavyPhysicsChainCount()
						: 0;
					m_AudioFeedback->OnCollisionImpact(
						value,
						existingHeavyChainCount);
				}
			}
			else if constexpr (std::is_same_v<EventType, WallImpactEvent>)
			{
				if (m_AudioFeedback != nullptr)
				{
					m_AudioFeedback->OnWallImpact(value);
				}
			}
			else if constexpr (std::is_same_v<EventType, EnemyDamageEvent>)
			{
				if (m_BattleState->controller.GetState() ==
					BattleState::BallsMoving)
				{
					++m_BattleState->presentationHitCount;
					m_BattleState->presentationDamage +=
						(std::max)(0, value.damage);
					if (value.defeated)
						++m_BattleState->presentationDefeatCount;
				}
				if (m_AudioFeedback != nullptr)
				{
					m_AudioFeedback->OnEnemyDamage(value);
				}
				m_PersistenceRuntime->Statistics().RecordEnemyDamage(value.damage);
				if (m_GamePresentation != nullptr &&
					!m_BalanceRuntime->autoPlayer.IsEnabled())
				{
					m_GamePresentation->OnCombatFeedback(
						value.worldPosition,
						value.damage,
						value.defeated,
						value.enemyEnemyCollision,
						value.targetKey,
						value.targetEnemyId);
				}
			}
			else if constexpr (std::is_same_v<EventType, ChainImpactEvent>)
			{
				if (m_GamePresentation != nullptr &&
					!m_BalanceRuntime->autoPlayer.IsEnabled())
				{
					m_GamePresentation->OnChainImpact(
						value.worldPosition,
						value.radius,
						value.hitCount);
				}
			}
			else if constexpr (std::is_same_v<EventType, PocketFeedbackEvent>)
			{
				if (!value.playerPocket && value.finisher)
				{
					m_PersistenceRuntime->Statistics().RecordEnemyDamage(value.damage);
				}
				if (m_GamePresentation != nullptr &&
					!m_BalanceRuntime->autoPlayer.IsEnabled())
				{
					m_GamePresentation->OnPocketFeedback(
						value.worldPosition,
						value.playerPocket,
						value.finisher,
						value.damage,
						value.targetKey,
						value.targetEnemyId);
				}
			}
			else if constexpr (std::is_same_v<EventType, PlayerDamageEvent>)
			{
				m_PersistenceRuntime->Statistics().RecordPlayerDamage(value.damage);
				BalanceLogger::GetInstance().RecordPlayerDamage(
					value.source,
					value.damage,
					value.sourceId);
				if (m_GamePresentation != nullptr &&
					!m_BalanceRuntime->autoPlayer.IsEnabled())
				{
					m_GamePresentation->OnPlayerDamage(
						value.damage,
						value.source);
				}
			}
			else if constexpr (std::is_same_v<EventType, AxisResourceChangedEvent>)
			{
				const std::size_t axis = static_cast<std::size_t>(value.axis);
				if (axis < m_BattleState->presentationResourceGained.size())
				{
					const int delta = value.valueAfter - value.valueBefore;
					if (delta > 0)
						m_BattleState->presentationResourceGained[axis] += delta;
					else
						m_BattleState->presentationResourceConsumed[axis] += -delta;
					if (value.finisher)
						m_BattleState->presentationFinisherMask |=
							(1u << static_cast<unsigned int>(axis));
				}
				if (m_GamePresentation != nullptr &&
					(!m_BalanceRuntime->autoPlayer.IsEnabled() || IsDebugMode()))
				{
					m_GamePresentation->OnAxisResourceChanged(value);
				}
			}
			else if constexpr (std::is_same_v<EventType, AxisAbilityResolvedEvent>)
			{
				if (m_GamePresentation != nullptr &&
					(!m_BalanceRuntime->autoPlayer.IsEnabled() || IsDebugMode()))
				{
					m_GamePresentation->OnAxisAbilityResolved(value);
				}
			}
			else if constexpr (std::is_same_v<EventType, ChargeInterruptedEvent>)
			{
				++m_BattleState->presentationChargeInterruptions;
				if (m_GamePresentation != nullptr &&
					(!m_BalanceRuntime->autoPlayer.IsEnabled() || IsDebugMode()))
				{
					m_GamePresentation->OnChargeInterrupted(
						value.worldPosition,
						static_cast<ChargeInterruptReason>(value.reason));
				}
			}
			else if constexpr (std::is_same_v<EventType, EnemyAbilityStateChangedEvent>)
			{
				if (m_GamePresentation != nullptr &&
					(!m_BalanceRuntime->autoPlayer.IsEnabled() || IsDebugMode()))
				{
					m_GamePresentation->OnEnemyAbilityStateChanged(value);
				}
			}
			else if constexpr (std::is_same_v<EventType, ShotResolvedEvent>)
			{
				if (m_GamePresentation != nullptr &&
					(!m_BalanceRuntime->autoPlayer.IsEnabled() || IsDebugMode()))
				{
					m_GamePresentation->OnShotResolved(value);
				}
			}
			else if constexpr (std::is_same_v<EventType, BallAcquiredEvent>)
			{
				m_PersistenceRuntime->Statistics().RecordBallAcquired(value.ballId);
			}
		},
		event);
}

void Game::NotifyEnemyAbilityStateChanged(
	const EnemyAbilityStateChangedEvent& event)
{
	const auto enemies = m_RuntimeState->World().GetComponents<EnemyBall>();
	const EnemyBall* changedEnemy = reinterpret_cast<const EnemyBall*>(
		event.targetKey);
	const auto found = std::find(
		enemies.begin(), enemies.end(), changedEnemy);
	const std::string targetId = found != enemies.end()
		? "enemy:" + std::to_string(
			static_cast<std::size_t>(std::distance(enemies.begin(), found)))
		: std::string();
	const EnemyActionType plannedAction = changedEnemy != nullptr
		? changedEnemy->GetPlannedAction()
		: EnemyActionType::Wait;
	BalanceLogger::GetInstance().RecordEvent(
		"enemy_board_ability_state_changed",
		{
			{ "enemy_id", event.targetEnemyId },
			{ "target_id", targetId },
			{ "ability_id", event.abilityId },
			{ "reason", event.reason },
			{ "previous_stage", event.previousStage },
			{ "current_stage", event.currentStage },
			{ "previous_attack", event.previousAttack },
			{ "current_attack", event.currentAttack },
			{ "nearby_living_allies", event.nearbyLivingAllies },
			{ "collision_speed", event.collisionSpeed },
			{ "planned_action", ToString(plannedAction) },
			{ "affects_next_turn",
				plannedAction == EnemyActionType::NormalAttack ||
				plannedAction == EnemyActionType::ChargeAttack },
		});
	PublishGameEvent(event);
}

