#include "Game.h"

#include "GameBattleState.h"
#include "GameBalanceRuntime.h"
#include "GamePersistenceRuntime.h"
#include "GameRunDefaults.h"
#include "GameRewardState.h"
#include "GameEncounterBalance.h"
#include "GameStageOverrideState.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "RunController.h"
#include "RunProgressConstants.h"
#include "StageSelector.h"

#pragma execution_character_set("utf-8")
#include "json/json.hpp"
#include "GameDebugController.h"
#include "RunProgressController.h"
#include "Renderer.h"
#include "BalanceLogger.h"
#include "GameMcpBridge.h"
#include "BallPhysicsComponent.h"
#include "BallPhysicsWorld.h"
#include "input.h"

#include "PlayerBall.h"  // DrawImGui呼び出しに必要
#include "EnemyBall.h"   // DrawImGui呼び出しに必要
#include "EnemyAttackComponent.h"
#include "BallComponent.h"
#include "PlayerBallDataLoader.h"
#include "StageDataLoader.h"
#include "EnemyData.h"
#include "TableConfig.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

GameBalanceAutomationSnapshot
GameBalanceRuntime::CaptureAutomationSnapshot() const
{
	GameBalanceAutomationSnapshot snapshot;
	snapshot.enabled = autoPlayer.IsEnabled();
	snapshot.stopAfterCurrentRunRequested =
		autoPlayer.IsStopAfterCurrentRunRequested();
	snapshot.stopAfterCurrentRunDefault =
		autoPlayer.IsStopAfterCurrentRunDefault();
	snapshot.runCount = autoPlayer.GetRunCount();
	snapshot.maximumRuns = autoPlayer.GetMaximumRuns();
	return snapshot;
}

GameBalanceReadSnapshot GameBalanceRuntime::CaptureReadSnapshot(
	int progress,
	int clearedStageCountValue) const
{
	GameBalanceReadSnapshot snapshot;
	snapshot.automation = CaptureAutomationSnapshot();
	snapshot.pendingBallAdjustments = autoPlayer.GetPendingBallAdjustments();

	snapshot.progressionScalingEnabled =
		scaling.IsProgressionScalingEnabled();
	snapshot.currentProgress = progress;
	snapshot.nextEnemyHpDelta =
		scaling.CalculateProgressionHpModifier(progress);
	snapshot.progressionHpStart = scaling.GetProgressionHpStart();
	snapshot.progressionHpInterval = scaling.GetProgressionHpInterval();
	snapshot.progressionHpStep = scaling.GetProgressionHpStep();
	snapshot.progressionHpMaximumDelta =
		scaling.GetProgressionHpMaximumDelta();
	snapshot.nextEnemyAttackDelta =
		scaling.CalculateProgressionAttackModifier(progress);
	snapshot.progressionAttackStart = scaling.GetProgressionAttackStart();
	snapshot.progressionAttackInterval = scaling.GetProgressionAttackInterval();
	snapshot.progressionAttackStep = scaling.GetProgressionAttackStep();
	snapshot.progressionAttackMaximumDelta =
		scaling.GetProgressionAttackMaximumDelta();

	snapshot.baselineDifficultyProfile = baselineDifficultyProfile;
	snapshot.baselineEnemyHpMultiplier = baselineEnemyHpMultiplier;
	snapshot.baselineEnemyAttackDelta = baselineEnemyAttackDelta;

	snapshot.validationEnabled = validation.IsEnabled();
	snapshot.validationExperimentId = validation.GetExperimentId();
	snapshot.validationVariantId = validation.GetCurrentVariantId();
	snapshot.validationVariantIndex = validation.GetVariantIndex();
	snapshot.validationVariantCount = validation.GetVariants().size();
	snapshot.runRandomSeed = runRandomSeed;
	snapshot.stageSelectionSeed = stageSelectionSeed;
	snapshot.routeSelectionSeed = routeSelectionSeed;
	snapshot.validationSeedIndex = validation.GetSeedIndex();
	snapshot.validationSeedCount = validation.GetSeedCount();
	snapshot.validationMaximumClearedStages =
		validation.GetMaximumClearedStages();
	snapshot.validationEnduranceMode = validation.IsEnduranceMode();
	snapshot.clearedStageCount = clearedStageCountValue;
	return snapshot;
}

bool GameBalanceRuntime::IsAutoPlayEnabled() const
{
	return autoPlayer.IsEnabled();
}

void GameBalanceRuntime::SetAutoPlayEnabled(bool enabled)
{
	autoPlayer.SetEnabled(enabled);
}

void GameBalanceRuntime::SetStopAfterCurrentRunRequested(bool requested)
{
	autoPlayer.SetStopAfterCurrentRunRequested(requested);
}

bool GameBalanceRuntime::IsValidationEnabled() const
{
	return validation.IsEnabled();
}

void GameBalanceRuntime::SetValidationEnabled(bool enabled)
{
	validation.SetEnabled(enabled);
}

bool GameBalanceRuntime::HasValidationVariant(
	const std::string& variantId) const
{
	return validation.HasVariant(variantId);
}

bool Game::IsEnduranceMode() const
{
	return m_BalanceRuntime->validation.IsEnduranceMode();
}

// Player Status From Jsonを読み込む。
void Game::LoadPlayerStatusFromJson(
	const std::string& filePath,
	const std::string& deckFilePath)
{
	PlayerBallDataLoadResult loadResult =
		PlayerBallDataLoader::Load(
			filePath,
			m_RunDefaults->PlayerBallStatus(),
			m_RunDefaults->PlayerStatus()
		);

	m_RunDefaults->PlayerBallStatus() = loadResult.defaultBallStatus;
	m_RunDefaults->PlayerStatus() = NormalizePlayerRunStatus(
		loadResult.defaultRunStatus
	);
	m_RunController->RestHealRatio() = loadResult.restHealRatio;
	m_PersistenceRuntime->DefaultRestHealRatio() = loadResult.restHealRatio;
	std::vector<PlayerBallData> unlockedCatalog;
	for (const auto& ball : loadResult.ballDefinitions)
		if (m_PersistenceRuntime->Profile().IsBallUnlocked(ball.definitionId))
			unlockedCatalog.push_back(ball);
	m_RunController->Deck().SetCatalog(unlockedCatalog);

	std::vector<PlayerBallData> defaultDeck =
		PlayerBallDataLoader::LoadDeck(
			deckFilePath,
			loadResult.ballDefinitions
		);
	m_RunController->Deck().SetDefaultDeck(defaultDeck);

	ResetPlayerRuntimeStatus();
}
// Player Runtime Statusを初期状態へ戻す。
void Game::ResetPlayerRuntimeStatus()
{
	// プレイヤーとラン進行に属する一時状態を新規ラン開始前の値へ戻す。
	m_RunController->ResetRuntimeState(m_RunDefaults->PlayerStatus());
	ResetShotRelicState();
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
	m_RewardState->ResetForRun();
	m_RunController->Progress().Reset(0);
	m_BalanceRuntime->autoPlayer.ResetPendingBallAdjustments();
	m_BattleState->controller.BeginStage(StageType::Normal);
	m_StageOverrideState->ClearCurrent();
	m_DebugController->ResetDiagnostics("ラン初期化");
}

// New Runを開始する。
void Game::StartNewRun(
	const std::string& controllerType,
	const std::string& controllerProfile,
	const std::string& buildProfile,
	const std::string& buildProfileSettingsHash,
	std::optional<std::uint32_t> forcedRandomSeed,
	const std::string& forcedValidationVariant)
{
	if (!IsDebugMode()) LoadPlayerStatusFromJson(); // Refresh the catalog after newly completed achievements.
	ResetPlayerRuntimeStatus();
	const std::string effectiveControllerType = !controllerType.empty()
		? controllerType
		: (m_BalanceRuntime->autoPlayer.IsEnabled() ? "autoplay" : "human");
	m_PersistenceRuntime->PersistentProgressEligible() =
		effectiveControllerType == "human" && !IsDebugMode() &&
		!m_BalanceRuntime->autoPlayer.IsEnabled() &&
		!m_BalanceRuntime->validation.IsEnabled();
	m_PersistenceRuntime->ActiveAscension() = (!IsDebugMode() &&
		!m_BalanceRuntime->autoPlayer.IsEnabled() &&
		!m_BalanceRuntime->validation.IsEnabled())
		? m_PersistenceRuntime->Profile().selectedAscension : 0;
	const int hpPenalty = ProgressionProfile::StartingHpPenalty(
		m_PersistenceRuntime->ActiveAscension());
	m_RunController->Status().maxHp = (std::max)(1, m_RunController->Status().maxHp - hpPenalty);
	m_RunController->Status().currentHp = m_RunController->Status().maxHp;
	m_RunController->RestHealRatio() = (std::max)(
		0.05f,
		m_PersistenceRuntime->DefaultRestHealRatio() -
			ProgressionProfile::RestHealPenalty(
				m_PersistenceRuntime->ActiveAscension()));
	m_PersistenceRuntime->Statistics().Reset();
	BeginRunSession();
	ResumeAndDismissPause();
	*m_PendingShotTelemetry = nlohmann::json::object();
	const BalanceValidationRun validationRun =
		m_BalanceRuntime->validation.OnRunStarted(
			forcedValidationVariant,
			forcedRandomSeed);
	if (validationRun.enabled)
	{
		m_BalanceRuntime->runRandomSeed = validationRun.seed;
	}
	else if (m_BalanceRuntime->autoPlayer.IsEnabled())
	{
		m_BalanceRuntime->runRandomSeed =
			m_BalanceRuntime->autoPlayer.GetRandomSeed() +
			static_cast<std::uint32_t>(m_BalanceRuntime->autoPlayer.GetRunCount());
	}
	else
	{
		m_BalanceRuntime->runRandomSeed = std::random_device{}();
	}
	if (forcedRandomSeed.has_value())
	{
		m_BalanceRuntime->runRandomSeed = forcedRandomSeed.value();
	}
	m_RunController->Deck().Seed(m_BalanceRuntime->runRandomSeed ^ 0x165667b1u);
	// ResetPlayerRuntimeStatusは通常乱数で初期化されるため、
	// ランシード確定後に山札を再構成し、前後比較を再現可能にする。
	m_RunController->Deck().ResetToDefault();
	m_BalanceRuntime->stageSelectionSeed = m_BalanceRuntime->runRandomSeed ^ 0x9e3779b9u;
	m_BalanceRuntime->routeSelectionSeed = m_BalanceRuntime->runRandomSeed ^ 0x85ebca6bu;
	m_BalanceRuntime->routeSelectionCounter = 0;
	m_RunController->Progress().Reset(m_BalanceRuntime->routeSelectionSeed);
	m_RunController->StageSelection().Seed(m_BalanceRuntime->stageSelectionSeed);
	m_BalanceRuntime->autoPlayer.SeedRandom(m_BalanceRuntime->runRandomSeed ^ 0xc2b2ae35u);
	m_BattleState->controller.PocketRandomEngine().seed(m_BalanceRuntime->runRandomSeed ^ 0x27d4eb2fu);
	m_RunController->SeedRelics(m_BalanceRuntime->runRandomSeed ^ 0xd3a2646cu);
	if (IsDebugMode()) ApplyDebugRunSettings();

	std::vector<BalanceBallSnapshot> deck;
	deck.reserve(static_cast<size_t>(
		m_RunController->Deck().GetRewardTargetCount()));

	for (int index = 0;
		index < m_RunController->Deck().GetRewardTargetCount();
		index++)
	{
		const PlayerBallData* ball =
			m_RunController->Deck().GetRewardTarget(index);
		if (ball == nullptr)
		{
			continue;
		}

		BalanceBallSnapshot snapshot;
		snapshot.id = ball->definitionId;
		snapshot.instanceId = ball->instanceId;
		snapshot.upgradeLevel = ball->upgradeLevel;
		snapshot.attack = ball->status.attack;
		snapshot.mass = ball->status.mass;
		snapshot.radius = ball->status.radius;
		snapshot.restitution = ball->status.restitution;
		snapshot.friction = ball->status.friction;
		snapshot.split = ball->status.abilities.split;
		snapshot.pierce = ball->status.abilities.pierce;
		snapshot.anchor = ball->status.abilities.anchor;
		deck.push_back(std::move(snapshot));
	}

	const nlohmann::json runContext =
	{
		{ "debug_sandbox", IsDebugMode() },
		{ "physics_model", BallPhysicsWorld::ModelName },
		{ "controller_profile", controllerProfile },
		{ "build_profile", buildProfile },
		{ "build_profile_settings_hash", buildProfileSettingsHash },
		{
			"randomness",
			{
				{ "run_seed", m_BalanceRuntime->runRandomSeed },
				{ "autoplay_seed", m_BalanceRuntime->runRandomSeed ^ 0xc2b2ae35u },
				{ "stage_selection_seed", m_BalanceRuntime->stageSelectionSeed },
				{ "route_selection_seed", m_BalanceRuntime->routeSelectionSeed },
			}
		},
		{
			"validation",
			{
				{ "enabled", m_BalanceRuntime->validation.IsEnabled() },
				{ "experiment_id", m_BalanceRuntime->validation.GetExperimentId() },
				{ "variant_id", m_BalanceRuntime->validation.GetCurrentVariantId() },
				{ "variant_index", m_BalanceRuntime->validation.GetVariantIndex() },
				{ "variant_count", m_BalanceRuntime->validation.GetVariants().size() },
				{ "maximum_cleared_stages",
					m_BalanceRuntime->validation.GetMaximumClearedStages() },
				{
					"dynamic_balance_forced_off",
					true
				},
				{ "fixed_stage_schedule", m_BalanceRuntime->validation.UsesFixedStageSchedule() },
				{ "endurance_mode", m_BalanceRuntime->validation.IsEnduranceMode() },
				{ "seed_suite_index", m_BalanceRuntime->validation.GetSeedIndex() },
				{ "seed_suite_size", m_BalanceRuntime->validation.GetSeedCount() },
			}
		},
		{
			"baseline_difficulty",
			{
				{ "profile", m_BalanceRuntime->baselineDifficultyProfile },
				{ "enemy_hp_multiplier", m_BalanceRuntime->baselineEnemyHpMultiplier },
				{ "enemy_attack_delta", m_BalanceRuntime->baselineEnemyAttackDelta },
			}
		},
		{ "dynamic_balance_enabled_at_start", false },
		{ "dynamic_balance_level_at_start", 0 },
		{ "initial_money", m_RunController->Status().money },
		{ "initial_progress", m_RunController->Status().progress },
		{ "ascension", m_PersistenceRuntime->ActiveAscension() },
		{ "area_goal", kNormalRouteAreaGoal },
		{ "run_map", m_RunController->Progress().GetMap().Snapshot() },
		{ "run_phase", ToString(m_RunController->Progress().GetPhase()) },
	};

	BalanceLogger::GetInstance().BeginRun(
		m_RunController->Status().maxHp,
		m_RunController->Status().currentHp,
		deck,
		effectiveControllerType,
		runContext);
}

// Difficulty Profile Configを読み込む。
void Game::LoadDifficultyProfileConfig(
	const std::string& filePath)
{
	std::ifstream file(filePath);
	if (!file)
	{
		std::cout << "[DifficultyProfile] Config not found: "
			<< filePath << std::endl;
		return;
	}

	try
	{
		nlohmann::json config;
		file >> config;
		const std::string selected = config.value(
			"selected_profile",
			std::string("normal"));
		const nlohmann::json& profiles = config.at("profiles");
		if (!profiles.contains(selected) ||
			!profiles[selected].is_object())
		{
			throw std::runtime_error(
				"selected_profile does not exist in profiles");
		}
		const nlohmann::json& profile = profiles[selected];
		m_BalanceRuntime->baselineDifficultyProfile = selected;
		m_BalanceRuntime->baselineEnemyHpMultiplier = std::clamp(
			profile.value("enemy_hp_multiplier", 1.0f),
			0.25f,
			4.0f);
		m_BalanceRuntime->baselineEnemyAttackDelta = std::clamp(
			profile.value("enemy_attack_delta", 0),
			-10,
			10);
		std::cout << "[DifficultyProfile] " << selected
			<< " / HP x" << m_BalanceRuntime->baselineEnemyHpMultiplier
			<< " / ATK " << m_BalanceRuntime->baselineEnemyAttackDelta
			<< std::endl;
	}
	catch (const std::exception& error)
	{
		std::cerr << "[DifficultyProfile] Invalid config: "
			<< error.what() << std::endl;
	}
}

// Encounter Balance Configを読み込む。
void Game::LoadEncounterBalanceConfig(
	const std::string& filePath)
{
	m_EncounterBalance->Load(filePath);
}

// Pocket Rules Configを読み込む。
void Game::LoadPocketRulesConfig(const std::string& filePath)
{
	std::ifstream file(filePath);
	if (!file)
	{
		std::cout << "[PocketRules] Config not found: "
			<< filePath << std::endl;
		return;
	}

	try
	{
		nlohmann::json config;
		file >> config;
		auto& rules = m_BattleState->controller.PocketRules();
		rules.playerDamageRatio = std::clamp(
			config.value("player_max_hp_damage_ratio",
				rules.playerDamageRatio),
			0.0f,
			1.0f);
		const nlohmann::json finishers = config.value(
			"enemy_finisher_hp_ratios",
			nlohmann::json::object());
		rules.normalFinisherRatio = std::clamp(
			finishers.value("normal", rules.normalFinisherRatio),
			0.0f,
			1.0f);
		rules.midBossFinisherRatio = std::clamp(
			finishers.value("midboss", rules.midBossFinisherRatio),
			0.0f,
			1.0f);
		rules.bossFinisherRatio = std::clamp(
			finishers.value("boss", rules.bossFinisherRatio),
			0.0f,
			1.0f);
		const nlohmann::json playerReturn = config.value(
			"player_return_region",
			nlohmann::json::object());
		rules.playerReturnHalfWidth = (std::max)(
			0.0f,
			playerReturn.value(
				"half_width", rules.playerReturnHalfWidth));
		rules.playerReturnHalfDepth = (std::max)(
			0.0f,
			playerReturn.value(
				"half_depth", rules.playerReturnHalfDepth));
		const nlohmann::json enemyReturn = config.value(
			"enemy_return",
			nlohmann::json::object());
		const nlohmann::json enemyReturnPosition = enemyReturn.value(
			"position",
			nlohmann::json::object());
		rules.enemyReturnX = enemyReturnPosition.value(
			"x", rules.enemyReturnX);
		rules.enemyReturnTopEdgeOffset = (std::max)(
			0.0f,
			enemyReturnPosition.value(
				"z_from_top_edge",
				rules.enemyReturnTopEdgeOffset));
	}
	catch (const nlohmann::json::exception& error)
	{
		std::cerr << "[PocketRules] Invalid config: "
			<< error.what() << std::endl;
	}
}
