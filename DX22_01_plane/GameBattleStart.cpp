#include "Game.h"
#include "GameBattleState.h"
#include "GameBalanceRuntime.h"
#include "GamePersistenceRuntime.h"
#include "GameEncounterBalance.h"
#include "GameStageOverrideState.h"
#include "PlayerBallData.h"
#include "PlayerRunStatus.h"
#include "RunController.h"
#include "RunProgressConstants.h"
#include "BalanceLogger.h"
#include "GamePresentation.h"
#include "RunProgressController.h"
#include "StageData.h"
#include <utility>
#include <vector>

#pragma execution_character_set("utf-8")

// On Battle Stage Started の処理を実行
void Game::OnBattleStageStarted(const StageData& stage)
{
	m_BattleState->controller.SetEnemyActionMaximumBudget(
		m_BalanceRuntime->GetEnemyActionMaximumBudget(
			m_RunController->Status().progress));
	m_BattleState->controller.BeginStage(stage);
	if (m_BattleState->controller.IsPhasePrototype())
	{
		RecordBalanceEvent("phase_started", {
			{ "phase", m_BattleState->controller.GetCurrentPhase() },
			{ "total_phases", m_BattleState->controller.GetTotalPhases() },
			{ "spawned_enemy_count", stage.phaseBattle.phases.empty()
				? 0
				: static_cast<int>(
					stage.phaseBattle.phases.front().enemies.size()) },
			{ "floor", m_RunController->Status().progress },
		});
	}
	m_BattleState->cushionCharges = {};
	m_BattleState->cushionBoostConsumedThisShot = false;
	m_BattleState->cushionStrongUsesThisShot = 0;
	m_BattleState->synergyDamageBonusThisShot = 0;
	HeavyCollisionRules::Reset(m_BattleState->heavyCollisions);
	m_BattleState->pierceTraces = {};
	m_BattleState->anchorStacks = {};
	m_BattleState->piercedEnemiesThisShot.clear();
	m_BattleState->anchorContactTarget = nullptr;
	m_BattleState->playerShield = 0;
	InvalidateDebugCombatForecast("戦闘開始");
	m_PersistenceRuntime->Statistics().ReachFloor(
		m_RunController->Status().progress);
	if (m_GamePresentation != nullptr &&
		!m_BalanceRuntime->autoPlayer.IsEnabled())
	{
		m_GamePresentation->OnBattleStarted(*this);
	}
	m_PersistenceRuntime->Statistics().BeginBattle(
		stage.stageType == StageType::MidBoss,
		stage.stageType == StageType::Boss &&
			m_RunController->Progress().GetPhase() == RunPhase::FinalBoss,
		stage.id);
	std::vector<BalanceEnemySnapshot> enemies;
	enemies.reserve(stage.enemies.size());

	for (const EnemySpawnData& spawn : stage.enemies)
	{
		const BallStatus& status = spawn.enemyData.status;
		BalanceEnemySnapshot snapshot;
		snapshot.id = spawn.enemyData.id;
		snapshot.maxHp = spawn.enemyData.maxHp;
		snapshot.attack = status.attack;
		snapshot.defense = status.defense;
		snapshot.mass = status.mass;
		snapshot.radius = status.radius;
		snapshot.restitution = status.restitution;
		snapshot.friction = status.friction;
		snapshot.positionX = spawn.position.x;
		snapshot.positionY = spawn.position.y;
		snapshot.positionZ = spawn.position.z;
		enemies.push_back(std::move(snapshot));
	}

	nlohmann::json deckJson = nlohmann::json::array();
	for (int index = 0;
		index < GetDeckBallCount();
		index++)
	{
		const PlayerBallData* ball =
			GetDeckBall(index);
		if (ball == nullptr)
		{
			continue;
		}
		deckJson.push_back(
			{
				{ "instance_id", ball->instanceId },
				{ "id", ball->definitionId },
				{ "category", BallCategoryId(ball->category) },
				{ "upgrade_level", ball->upgradeLevel },
				{ "attack", GetEffectivePlayerBallAttack(ball) },
			});
	}

	nlohmann::json relicsJson = nlohmann::json::array();
	for (const RelicDefinition& relic : RelicCatalog)
	{
		if (HasRelic(relic.type))
		{
			relicsJson.push_back(relic.name);
		}
	}

	constexpr int appliedHpModifier = 0;
	constexpr int appliedAttackModifier = 0;
	const int progressionAttackModifier =
		m_BalanceRuntime->scaling.CalculateProgressionAttackModifier(
			m_RunController->Status().progress);
	const int progressionHpModifier =
		m_BalanceRuntime->scaling.CalculateProgressionHpModifier(
			m_RunController->Status().progress);
	EncounterThreatInput encounterInput;
	encounterInput.stageId = stage.id;
	encounterInput.usesMcpOverride =
		m_StageOverrideState->HasCurrent();
	encounterInput.usesDenseAutoLayout =
		!encounterInput.usesMcpOverride &&
		!stage.preserveLayout &&
		stage.enemies.size() >= 4;
	encounterInput.enemyIds.reserve(stage.enemies.size());
	for (const EnemySpawnData& spawn : stage.enemies)
	{
		encounterInput.enemyIds.push_back(spawn.enemyId);
	}
	const EncounterThreatSnapshot encounterThreat =
		m_EncounterBalance->Evaluate(encounterInput);
	const bool validationEnabled =
		stage.enemyValidation.has_value() &&
		stage.enemyValidation->enabled;
	nlohmann::json phaseEnemyCounts = nlohmann::json::array();
	int scheduledEnemyCount = 0;
	if (stage.combatMode == CombatMode::PhasePrototype)
	{
		for (const EnemyPhaseData& phase : stage.phaseBattle.phases)
		{
			const int enemyCount = static_cast<int>(phase.enemies.size());
			phaseEnemyCounts.push_back(enemyCount);
			scheduledEnemyCount += enemyCount;
		}
	}
	else
	{
		phaseEnemyCounts.push_back(static_cast<int>(stage.enemies.size()));
		scheduledEnemyCount = static_cast<int>(stage.enemies.size());
	}
	const nlohmann::json enemyValidationContext =
		validationEnabled
		? nlohmann::json{
			{ "enabled", true },
			{ "hp_multiplier", stage.enemyValidation->hpMultiplier },
			{ "enemy_count", static_cast<int>(stage.enemies.size()) },
			{ "layout_pattern", EnemyValidationLayoutPatternId(
				stage.enemyValidation->layoutPattern) },
			{ "seed", stage.enemyValidation->seed },
		}
		: nlohmann::json{
			{ "enabled", false },
			{ "hp_multiplier", 1.0f },
			{ "enemy_count", static_cast<int>(stage.enemies.size()) },
			{ "layout_pattern", "existing" },
			{ "seed", nullptr },
		};
	const nlohmann::json stageContext =
	{
		{ "run_combat_mode", ToString(m_RunController->GetCombatMode()) },
		{ "phase_progression_mode",
			ToString(m_RunController->GetPhaseProgressionMode()) },
		{ "phase_normal_battle_encounter_count",
			m_RunController->Progress().GetPhaseNormalBattleEncounterCount() },
		{ "combat_mode", ToString(stage.combatMode) },
		{ "phase_count", static_cast<int>(phaseEnemyCounts.size()) },
		{ "phase_enemy_counts", phaseEnemyCounts },
		{ "scheduled_enemy_count", scheduledEnemyCount },
		{ "progress", m_RunController->Status().progress },
		{ "area_progress", m_RunController->Progress().GetAreaProgress() },
		{ "area_goal", kNormalRouteAreaGoal },
		{ "run_phase", ToString(m_RunController->Progress().GetPhase()) },
		{ "par", stage.par },
		{ "money", m_RunController->Status().money },
		{ "layout_source", encounterThreat.layoutSource },
		{ "deck", std::move(deckJson) },
		{ "owned_relics", std::move(relicsJson) },
		{ "enemy_validation", enemyValidationContext },
		{
			"baseline_difficulty",
			{
				{ "profile", m_BalanceRuntime->baselineDifficultyProfile },
				{ "enemy_hp_multiplier", m_BalanceRuntime->baselineEnemyHpMultiplier },
				{ "enemy_attack_delta", m_BalanceRuntime->baselineEnemyAttackDelta },
			}
		},
		{
			"encounter_threat",
			{
				{ "base_budget", encounterThreat.baseBudget },
				{ "layout_multiplier", encounterThreat.layoutMultiplier },
				{ "actual_budget", encounterThreat.actualBudget },
				{
					"target_budget",
					encounterThreat.targetBudget.has_value()
						? nlohmann::json(encounterThreat.targetBudget.value())
						: nlohmann::json(nullptr)
				},
				{
					"deviation_from_target",
					encounterThreat.targetBudget.has_value()
						? nlohmann::json(
							encounterThreat.actualBudget -
							encounterThreat.targetBudget.value())
						: nlohmann::json(nullptr)
				},
			}
		},
		{
			"dynamic_balance",
			{
				{ "enabled", false },
				{ "applied_level", 0 },
				{ "enemy_hp_modifier", appliedHpModifier },
				{ "enemy_attack_modifier", appliedAttackModifier },
			}
		},
		{
			"progression_scaling",
			{
				{ "enabled", m_BalanceRuntime->scaling.IsProgressionScalingEnabled() },
				{ "progress", m_RunController->Status().progress },
				{ "enemy_hp_modifier", progressionHpModifier },
				{ "hp_start_progress", m_BalanceRuntime->scaling.GetProgressionHpStart() },
				{ "hp_interval", m_BalanceRuntime->scaling.GetProgressionHpInterval() },
				{ "hp_step", m_BalanceRuntime->scaling.GetProgressionHpStep() },
				{ "maximum_hp_delta",
					m_BalanceRuntime->scaling.GetProgressionHpMaximumDelta() },
				{ "enemy_attack_modifier", progressionAttackModifier },
				{ "attack_start_progress", m_BalanceRuntime->scaling.GetProgressionAttackStart() },
				{ "attack_interval", m_BalanceRuntime->scaling.GetProgressionAttackInterval() },
				{ "maximum_attack_delta",
					m_BalanceRuntime->scaling.GetProgressionAttackMaximumDelta() },
			}
		},
		{
			"assist_mode",
			{
				{ "enabled", false },
				{ "applied_level", 0 },
			}
		},
	};

	BalanceLogger::GetInstance().BeginStage(
		stage.id,
		ToString(stage.stageType),
		stage.difficulty,
		m_RunController->Status().currentHp,
		m_RunController->Status().maxHp,
		enemies,
		stageContext);
}
