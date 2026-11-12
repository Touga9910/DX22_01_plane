#include "Game.h"
#include "GameRuntime.h"
#include "GameRuntimeState.h"
#include "GameWorld.h"
#include "GameBattleState.h"
#include "PlayerDeck.h"
#include "RunController.h"
#include "BalanceLogger.h"
#include "BallComponent.h"
#include "BallFactory.h"
#include "PlayerBall.h"
#include "EnemyBall.h"
#include "PierceMinion.h"
#include "json/json.hpp"

#include <algorithm>

namespace
{
    int CountDefeatedEnemies(const std::vector<EnemyBall*>& enemies)
    {
        return static_cast<int>(std::count_if(
            enemies.begin(), enemies.end(),
            [](const EnemyBall* enemy)
            {
                return enemy != nullptr && enemy->IsDefeated();
            }));
    }

    int CountAliveEnemies(const std::vector<EnemyBall*>& enemies)
    {
        return static_cast<int>(std::count_if(
            enemies.begin(), enemies.end(),
            [](const EnemyBall* enemy)
            {
                return enemy != nullptr && !enemy->IsDefeated();
            }));
    }
}
void Game::ResetShotRelicState(PlayerBall* player)
{
	m_BattleState->controller.ResetShotState(MakePredictionShotRules(0.0f));
	if (player != nullptr)
	{
		ApplyRelicModifiersTo(player);
	}
}

// End Of Shot Relic Effectsを適用する。

void Game::OnPlayerShotFired(PlayerBall* player)
{
	ResetFrameTiming();
	ResetShotRelicState(player);
    // A new shot starts a new damage episode even for touching enemy pairs.
    for (auto* ball : GameRuntime::World().GetComponents<BallComponent>()) ball->ResetShotAbilityState();
	for (auto* enemy : GameRuntime::World().GetComponents<EnemyBall>())
	{
		enemy->BeginBossShot();
		enemy->BeginLightweightWindow();
	}

	// 選択内容はショットした瞬間に確定する。
	if (!m_RunController->Deck().HasCurrent())
	{
		if (!m_RunController->Deck().SelectOffer(
			GetSelectedBallOfferIndex(),
			GetHeldBallOfferIndex()))
		{
			return;
		}
	}
	if (IsFrameTimeMeasurementEnabled())
	{
		m_RuntimeState->BeginFrameTimingMeasurement();
	}

	CushionChargeRules::BeginPlayerShot(m_BattleState->cushionCharges);
	HeavyCollisionRules::BeginShot(m_BattleState->heavyCollisions);
	PierceTraceRules::BeginShot(m_BattleState->pierceTraces);
	m_BattleState->cushionBoostConsumedThisShot = false;
	m_BattleState->cushionStrongUsesThisShot = 0;
	m_BattleState->synergyDamageBonusThisShot = 0;
	m_BattleState->piercedEnemiesThisShot.clear();
	m_BattleState->heavyFinisherConsumedThisShot = false;
	m_BattleState->pierceFinisherPresentedThisShot = false;
	m_BattleState->anchorFinisherTriggeredThisShot = false;
	m_BattleState->anchorContactTarget = nullptr;
	m_BattleState->playerShield = 0;
	m_BattleState->presentationHitCount = 0;
	m_BattleState->presentationDamage = 0;
	m_BattleState->presentationDefeatCount = 0;
	m_BattleState->presentationChargeInterruptions = 0;
	m_BattleState->presentationResourceGained.fill(0);
	m_BattleState->presentationResourceConsumed.fill(0);
	m_BattleState->presentationFinisherMask = 0;

	if (player != nullptr)
	{
		CapturePlayerStatusFrom(player);
	}

	const PlayerBallData* currentBall =
		m_RunController->Deck().GetCurrent();
	const int heavyStacksAtShotStart =
		m_BattleState->heavyCollisions.stackCount;
	const int pierceStacksAtShotStart =
		m_BattleState->pierceTraces.stackCount;
	const int reflectionStacksAtShotStart =
		m_BattleState->cushionCharges.stackCount;
	const int pierceTraceCountAtShotStart = static_cast<int>(
		m_BattleState->pierceTraces.traces.size());
	int heavyStacksConsumedOnStart = 0;
	int pierceStacksConsumedOnStart = 0;
	bool pierceMinionActivationAttempted = false;
	bool pierceMinionFinisher = false;
	std::vector<PierceTraceRules::MinionLaunch> pierceMinionLaunches;
	if (currentBall != nullptr &&
		currentBall->definitionId == "player_collision_boost" &&
		currentBall->status.heavySetupStackConsume > 0 &&
		m_BattleState->heavyCollisions.stackCount >=
			currentBall->status.heavySetupStackConsume)
	{
		heavyStacksConsumedOnStart = HeavyCollisionRules::Consume(
			m_BattleState->heavyCollisions,
			currentBall->status.heavySetupStackConsume);
		HeavyCollisionRules::ArmNextEnemyCollisionMultiplier(
			m_BattleState->heavyCollisions,
			currentBall->status.heavyEnemyCollisionMultiplier);
		RecordBalanceEvent("heavy_collision_boost_armed", {
			{ "consumed", heavyStacksConsumedOnStart },
			{ "remaining", m_BattleState->heavyCollisions.stackCount },
			{ "multiplier", currentBall->status.heavyEnemyCollisionMultiplier },
		});
	}
	if (currentBall != nullptr &&
		(currentBall->definitionId == "player_trace_driver" ||
		 currentBall->definitionId == "player_minion_finisher"))
	{
		pierceMinionActivationAttempted = true;
		pierceMinionFinisher =
			currentBall->definitionId == "player_minion_finisher";
		const int consumeLimit = pierceMinionFinisher
			? PierceTraceRules::MaxStackCount
			: currentBall->status.pierceStackConsumeLimit;
		pierceStacksConsumedOnStart = PierceTraceRules::ConsumeStacks(
			m_BattleState->pierceTraces,
			consumeLimit);
		pierceMinionLaunches = PierceTraceRules::BuildMinionLaunches(
			m_BattleState->pierceTraces,
			pierceStacksConsumedOnStart,
			pierceMinionFinisher);
	}
	if (player != nullptr && currentBall != nullptr)
	{
		if (currentBall->status.abilities.returnToShotStart)
		{
			AnchorStackRules::SaveReturnPosition(
				m_BattleState->anchorStacks,
				player->GetPosition());
		}
		auto shotRules = m_BattleState->controller.GetShotRelicRules();
		shotRules.launchPower = player->GetVelocity().Length();
		m_BattleState->controller.SetShotRelicRules(shotRules);
		PublishGameEvent(ShotFiredEvent{
			currentBall->definitionId,
			static_cast<int>(currentBall->category),
		});

		const DirectX::SimpleMath::Vector3 position =
			player->GetPosition();
		if (heavyStacksConsumedOnStart > 0)
		{
			NotifyAxisResourceChanged(
				AxisResourceKind::Heavy,
				AxisResourceOwner::Player,
				heavyStacksAtShotStart,
				m_BattleState->heavyCollisions.stackCount,
				AxisResourceChangeReason::AbilityCost,
				currentBall->definitionId,
				false,
				position);
		}
		if (pierceStacksConsumedOnStart > 0 ||
			(pierceMinionFinisher && !pierceMinionLaunches.empty()))
		{
			NotifyAxisResourceChanged(
				AxisResourceKind::Pierce,
				AxisResourceOwner::Player,
				pierceStacksAtShotStart,
				m_BattleState->pierceTraces.stackCount,
				pierceMinionFinisher
					? AxisResourceChangeReason::Finisher
					: AxisResourceChangeReason::AbilityCost,
				currentBall->definitionId,
				pierceMinionFinisher && !pierceMinionLaunches.empty(),
				position);
		}
		const DirectX::SimpleMath::Vector3 velocity =
			player->GetVelocity();
		const std::vector<EnemyBall*> enemies =
			GameRuntime::World().GetComponents<EnemyBall>();
		nlohmann::json offers = nlohmann::json::array();
		for (int index = 0;
			index < m_RunController->Deck().GetOfferCount();
			index++)
		{
			const PlayerBallData* offer =
				m_RunController->Deck().GetOffer(index);
			if (offer == nullptr)
			{
				continue;
			}
			offers.push_back(
				{
					{ "offer_index", index },
					{ "instance_id", offer->instanceId },
					{ "id", offer->definitionId },
					{ "upgrade_level", offer->upgradeLevel },
					{ "held", m_RunController->Deck().WasHeldOffer(index) },
				});
		}
		nlohmann::json shotContext =
		{
			{ "selected_offer_index", GetSelectedBallOfferIndex() },
			{ "held_offer_index", GetHeldBallOfferIndex() },
			{ "selected_instance_id", currentBall->instanceId },
			{ "ball_category", BallCategoryId(currentBall->category) },
			{ "effective_attack", player->GetAttack() },
			{ "heavy_stacks_before", heavyStacksAtShotStart },
			{ "heavy_stacks_consumed_on_start",
				heavyStacksConsumedOnStart },
			{ "heavy_stack_gain_limit_per_turn",
				m_BattleState->heavyCollisions.gainLimitPerTurn },
			{ "heavy_stack_distance_per_stack",
				m_BattleState->heavyCollisions.distancePerStack },
			{ "pierce_stacks_before", pierceStacksAtShotStart },
			{ "pierce_stacks_consumed_on_start",
				pierceStacksConsumedOnStart },
			{ "pierce_stack_max", PierceTraceRules::MaxStackCount },
			{ "pierce_trace_count_before", pierceTraceCountAtShotStart },
			{ "pierce_minion_activation_attempted",
				pierceMinionActivationAttempted },
			{ "pierce_minions_planned",
				static_cast<int>(pierceMinionLaunches.size()) },
			{ "pierce_minion_finisher", pierceMinionFinisher },
			{ "reflection_stacks_before", reflectionStacksAtShotStart },
			{ "offers", std::move(offers) },
			{ "mcp_telemetry", *m_PendingShotTelemetry },
		};
		std::vector<BalanceShotEnemySnapshot> shotEnemies;
		shotEnemies.reserve(enemies.size());
		for (std::size_t index = 0; index < enemies.size(); ++index)
		{
			EnemyBall* enemy = enemies[index];
			if (enemy == nullptr || enemy->IsDefeated())
			{
				continue;
			}
			const DirectX::SimpleMath::Vector3 enemyPosition =
				enemy->GetPosition();
			shotEnemies.push_back({
				reinterpret_cast<std::uintptr_t>(enemy),
				index,
				enemy->GetEnemyId(),
				enemy->GetHP(),
				enemy->GetMaxHP(),
				enemyPosition.x,
				enemyPosition.z,
			});
		}

		BalanceLogger::GetInstance().BeginShot(
			currentBall->definitionId,
			currentBall->upgradeLevel,
			velocity.Length(),
			position.x,
			position.y,
			position.z,
			velocity.x,
			velocity.y,
			velocity.z,
			player->GetHP(),
			CountAliveEnemies(enemies),
			CountDefeatedEnemies(enemies),
			shotEnemies,
			shotContext);
		if (pierceMinionActivationAttempted)
		{
			for (std::size_t launchIndex = 0;
				launchIndex < pierceMinionLaunches.size();
				++launchIndex)
			{
				const auto& launch = pierceMinionLaunches[launchIndex];
				BallStatus minionStatus = currentBall->status;
				minionStatus.abilities = {};
				minionStatus.attack = launch.attack;
				minionStatus.knockbackTransfer = launch.knockbackMultiplier;
				BallFactory::CreatePierceMinion(
					*this,
					minionStatus,
					launch.start,
					launch.direction,
					PierceTraceRules::MinionLaunchSpeed);
				const bool overcharged = launch.attack >
					PierceTraceRules::MinionBaseAttack ||
					launch.knockbackMultiplier > 1.0f;
				PublishGameEvent(AxisAbilityResolvedEvent{
					AxisResourceKind::Pierce,
					AxisAbilityAction::PierceMinionLaunched,
					launch.start,
					launch.start + launch.direction * 8.0f,
					launch.attack,
					static_cast<int>(launchIndex) + 1,
					0.0f,
					overcharged,
					pierceMinionFinisher,
					0,
					0,
					currentBall->definitionId,
				});
			}
			RecordBalanceEvent("pierce_minions_launched", {
				{ "consumed", pierceStacksConsumedOnStart },
				{ "remaining", m_BattleState->pierceTraces.stackCount },
				{ "minion_count", static_cast<int>(pierceMinionLaunches.size()) },
				{ "finisher", pierceMinionFinisher },
			});
		}
		*m_PendingShotTelemetry = nlohmann::json::object();
	}

	m_RunController->Deck().MarkCurrentUsed();
	m_BattleState->controller.NotifyShotFired(
		currentBall != nullptr &&
		currentBall->category == BallCategory::Heavy);
}

