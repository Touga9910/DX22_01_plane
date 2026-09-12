#include "Game.h"

#pragma execution_character_set("utf-8")
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
#include <limits>
#include <random>
#include <stdexcept>


// Player Status From Jsonを読み込む。
void Game::LoadPlayerStatusFromJson(
	const std::string& filePath,
	const std::string& deckFilePath)
{
	PlayerBallDataLoadResult loadResult =
		PlayerBallDataLoader::Load(
			filePath,
			m_DefaultPlayerStatus,
			m_DefaultPlayerRunStatus
		);

	m_DefaultPlayerStatus = loadResult.defaultBallStatus;
	m_DefaultPlayerRunStatus = NormalizePlayerRunStatus(
		loadResult.defaultRunStatus
	);
	m_RunController.RestHealRatio() = loadResult.restHealRatio;
	m_DefaultRestHealRatio = loadResult.restHealRatio;
	std::vector<PlayerBallData> unlockedCatalog;
	for (const auto& ball : loadResult.ballDefinitions)
		if (m_ProgressionProfile.IsBallUnlocked(ball.definitionId)) unlockedCatalog.push_back(ball);
	m_RunController.Deck().SetCatalog(unlockedCatalog);

	std::vector<PlayerBallData> defaultDeck =
		PlayerBallDataLoader::LoadDeck(
			deckFilePath,
			loadResult.ballDefinitions
		);
	m_RunController.Deck().SetDefaultDeck(defaultDeck);

	ResetPlayerRuntimeStatus();
}
// Player Runtime Statusを初期状態へ戻す。
void Game::ResetPlayerRuntimeStatus()
{
	// プレイヤーとラン進行に属する一時状態を新規ラン開始前の値へ戻す。
	m_RunController.ResetRuntimeState(m_DefaultPlayerRunStatus);
	ResetShotRelicState();
	m_CushionCharges = {};
	m_CushionBoostConsumedThisShot = false;
	m_PlayerShield = 0;
	m_IsMidBossRelicSelectionActive = false;
	m_RunController.Progress().Reset(0);
	m_BalanceAutoPlayer.ResetPendingBallAdjustments();
	m_BattleController.BeginStage(StageType::Normal);
	m_McpCurrentStageOverride.reset();
	m_DebugController.ResetDiagnostics("ラン初期化");
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
	m_DynamicBalanceController.OnRunStarted();
	const std::string effectiveControllerType = !controllerType.empty()
		? controllerType
		: (m_BalanceAutoPlayer.IsEnabled() ? "autoplay" : "human");
	m_PersistentProgressEligible = effectiveControllerType == "human" && !IsDebugMode() &&
		!m_BalanceAutoPlayer.IsEnabled() &&
		!m_BalanceValidationController.IsEnabled();
	m_ActiveAscension = (!IsDebugMode() &&
		!m_BalanceAutoPlayer.IsEnabled() &&
		!m_BalanceValidationController.IsEnabled())
		? m_ProgressionProfile.selectedAscension : 0;
	const int hpPenalty = ProgressionProfile::StartingHpPenalty(m_ActiveAscension);
	m_RunController.Status().maxHp = (std::max)(1, m_RunController.Status().maxHp - hpPenalty);
	m_RunController.Status().currentHp = m_RunController.Status().maxHp;
	m_RunController.RestHealRatio() = (std::max)(0.05f, m_DefaultRestHealRatio - ProgressionProfile::RestHealPenalty(m_ActiveAscension));
	m_RunStatistics.Reset();
	m_RunActive = true;
	m_IsPaused = false;
	m_PauseConfirmTitle = false;
	m_LastRunResult = RunResultSnapshot{};
	m_PendingShotTelemetry = nlohmann::json::object();
	const BalanceValidationRun validationRun =
		m_BalanceValidationController.OnRunStarted(
			forcedValidationVariant,
			forcedRandomSeed);
	if (validationRun.enabled)
	{
		m_RunRandomSeed = validationRun.seed;
		if (validationRun.disableDynamicBalance)
		{
			m_DynamicBalanceController.ForceDisabled();
		}
	}
	else if (m_BalanceAutoPlayer.IsEnabled())
	{
		m_RunRandomSeed =
			m_BalanceAutoPlayer.GetRandomSeed() +
			static_cast<std::uint32_t>(m_BalanceAutoPlayer.GetRunCount());
	}
	else
	{
		m_RunRandomSeed = std::random_device{}();
	}
	if (forcedRandomSeed.has_value())
	{
		m_RunRandomSeed = forcedRandomSeed.value();
	}
	m_RunController.Deck().Seed(m_RunRandomSeed ^ 0x165667b1u);
	// ResetPlayerRuntimeStatusは通常乱数で初期化されるため、
	// ランシード確定後に山札を再構成し、前後比較を再現可能にする。
	m_RunController.Deck().ResetToDefault();
	m_StageSelectionSeed = m_RunRandomSeed ^ 0x9e3779b9u;
	m_RouteSelectionSeed = m_RunRandomSeed ^ 0x85ebca6bu;
	m_RouteSelectionCounter = 0;
	m_RunController.Progress().Reset(m_RouteSelectionSeed);
	m_RunController.StageSelection().Seed(m_StageSelectionSeed);
	m_BalanceAutoPlayer.SeedRandom(m_RunRandomSeed ^ 0xc2b2ae35u);
	m_BattleController.PocketRandomEngine().seed(m_RunRandomSeed ^ 0x27d4eb2fu);
	m_RunController.SeedRelics(m_RunRandomSeed ^ 0xd3a2646cu);
	if (IsDebugMode()) ApplyDebugRunSettings();

	std::vector<BalanceBallSnapshot> deck;
	deck.reserve(static_cast<size_t>(
		m_RunController.Deck().GetRewardTargetCount()));

	for (int index = 0;
		index < m_RunController.Deck().GetRewardTargetCount();
		index++)
	{
		const PlayerBallData* ball =
			m_RunController.Deck().GetRewardTarget(index);
		if (ball == nullptr)
		{
			continue;
		}

		BalanceBallSnapshot snapshot;
		snapshot.id = ball->definitionId;
		snapshot.instanceId = ball->instanceId;
		snapshot.upgradeLevel = ball->upgradeLevel;
		snapshot.attack = ball->status.attack;
		snapshot.defense = ball->status.defense;
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
				{ "run_seed", m_RunRandomSeed },
				{ "autoplay_seed", m_RunRandomSeed ^ 0xc2b2ae35u },
				{ "stage_selection_seed", m_StageSelectionSeed },
				{ "route_selection_seed", m_RouteSelectionSeed },
			}
		},
		{
			"validation",
			{
				{ "enabled", m_BalanceValidationController.IsEnabled() },
				{ "experiment_id", m_BalanceValidationController.GetExperimentId() },
				{ "variant_id", m_BalanceValidationController.GetCurrentVariantId() },
				{ "variant_index", m_BalanceValidationController.GetVariantIndex() },
				{ "variant_count", m_BalanceValidationController.GetVariants().size() },
				{ "maximum_cleared_stages",
					m_BalanceValidationController.GetMaximumClearedStages() },
				{
					"dynamic_balance_forced_off",
					m_BalanceValidationController.IsDynamicBalanceLockedOff()
				},
				{ "fixed_stage_schedule", m_BalanceValidationController.UsesFixedStageSchedule() },
				{ "endurance_mode", m_BalanceValidationController.IsEnduranceMode() },
				{ "seed_suite_index", m_BalanceValidationController.GetSeedIndex() },
				{ "seed_suite_size", m_BalanceValidationController.GetSeedCount() },
			}
		},
		{
			"baseline_difficulty",
			{
				{ "profile", m_BaselineDifficultyProfile },
				{ "enemy_hp_multiplier", m_BaselineEnemyHpMultiplier },
				{ "enemy_attack_delta", m_BaselineEnemyAttackDelta },
			}
		},
		{ "dynamic_balance_enabled_at_start", m_DynamicBalanceController.IsEnabled() },
		{ "dynamic_balance_level_at_start", m_DynamicBalanceController.GetLevel() },
		{ "initial_money", m_RunController.Status().money },
		{ "initial_progress", m_RunController.Status().progress },
		{ "ascension", m_ActiveAscension },
		{ "area_goal", kNormalRouteAreaGoal },
		{ "run_map", m_RunController.Progress().GetMap().Snapshot() },
		{ "run_phase", ToString(m_RunController.Progress().GetPhase()) },
	};

	BalanceLogger::GetInstance().BeginRun(
		m_RunController.Status().maxHp,
		m_RunController.Status().currentHp,
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
		m_BaselineDifficultyProfile = selected;
		m_BaselineEnemyHpMultiplier = std::clamp(
			profile.value("enemy_hp_multiplier", 1.0f),
			0.25f,
			4.0f);
		m_BaselineEnemyAttackDelta = std::clamp(
			profile.value("enemy_attack_delta", 0),
			-10,
			10);
		std::cout << "[DifficultyProfile] " << selected
			<< " / HP x" << m_BaselineEnemyHpMultiplier
			<< " / ATK " << m_BaselineEnemyAttackDelta
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
	std::ifstream file(filePath);
	if (!file)
	{
		std::cout << "[EncounterBalance] Config not found: "
			<< filePath << std::endl;
		return;
	}

	try
	{
		nlohmann::json config;
		file >> config;
		m_EnemyThreatCosts.clear();
		m_StageThreatTargets.clear();
		const nlohmann::json enemyCosts = config.value(
			"enemy_costs",
			nlohmann::json::object());
		for (auto iterator = enemyCosts.begin();
			iterator != enemyCosts.end(); ++iterator)
		{
			if (iterator.value().is_number())
			{
				m_EnemyThreatCosts[iterator.key()] =
					(std::max)(0.0f, iterator.value().get<float>());
			}
		}
		const nlohmann::json stageTargets = config.value(
			"stage_target_budgets",
			nlohmann::json::object());
		for (auto iterator = stageTargets.begin();
			iterator != stageTargets.end(); ++iterator)
		{
			if (iterator.value().is_number())
			{
				m_StageThreatTargets[iterator.key()] =
					(std::max)(0.0f, iterator.value().get<float>());
			}
		}
		const nlohmann::json layouts = config.value(
			"layout_multipliers",
			nlohmann::json::object());
		m_StageDataLayoutThreatMultiplier =
			(std::max)(0.1f, layouts.value("stage_data", 1.0f));
		m_DenseLayoutThreatMultiplier =
			(std::max)(0.1f, layouts.value("dense_auto_layout", 1.25f));
		m_McpLayoutThreatMultiplier =
			(std::max)(0.1f, layouts.value("mcp_override", 1.0f));
	}
	catch (const nlohmann::json::exception& error)
	{
		std::cerr << "[EncounterBalance] Invalid config: "
			<< error.what() << std::endl;
	}
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
		auto& rules = m_BattleController.PocketRules();
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
