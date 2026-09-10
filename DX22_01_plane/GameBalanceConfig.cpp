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
	m_RestHealRatio = loadResult.restHealRatio;
	m_DefaultRestHealRatio = loadResult.restHealRatio;
	std::vector<PlayerBallData> unlockedCatalog;
	for (const auto& ball : loadResult.ballDefinitions)
		if (m_ProgressionProfile.IsBallUnlocked(ball.definitionId)) unlockedCatalog.push_back(ball);
	m_PlayerDeck.SetCatalog(unlockedCatalog);

	std::vector<PlayerBallData> defaultDeck =
		PlayerBallDataLoader::LoadDeck(
			deckFilePath,
			loadResult.ballDefinitions
		);
	m_PlayerDeck.SetDefaultDeck(defaultDeck);

	ResetPlayerRuntimeStatus();
}
// Player Runtime Statusを初期状態へ戻す。
void Game::ResetPlayerRuntimeStatus()
{
	// プレイヤーとラン進行に属する一時状態を新規ラン開始前の値へ戻す。
	m_PlayerRunStatus = NormalizePlayerRunStatus(m_DefaultPlayerRunStatus);
	m_PlayerRunStatus.progress = 1;
	m_PlayerRunStatus.SetSelectedStageId("");
	m_PlayerRunStatus.SetLastStageId("");
	m_PlayerDeck.ResetToDefault();
	m_OwnedRelics.fill(false);
	m_CurrentShotCollisionAttackBonus = 0;
	m_CurrentShotPlayerEnemyCollisionCount = 0;
	m_CurrentShotEnemyEnemyCollisionCount = 0;
	m_CurrentShotWallCollisionCount = 0;
	m_CurrentShotBounceDamageBonus = 0;
	m_CurrentShotAnchorStopped = false;
	m_CurrentShotLaunchPower = 0.0f;
	m_BountyRewardClaimed = false;
	m_IsMidBossRelicSelectionActive = false;
	m_MidBossRelicOffers.clear();
	m_ShopRelicOffers.clear();
	m_RunProgress.Reset(0);
	m_AutoPendingBallAdjustments.clear();
	m_PocketedEnemyQueue.clear();
	m_McpCurrentStageOverride.reset();
	m_DebugPlayerDamageHistory.clear();
	m_DebugDamageSequence = 0;
	m_DebugLastEnemyAttackComparisonValid = false;
	InvalidateDebugCombatForecast("ラン初期化");
}

// Dynamic Balance Run Stateを初期状態へ戻す。
void Game::ResetDynamicBalanceRunState()
{
	m_DynamicBalanceEnabled = m_DynamicBalanceConfiguredEnabled;
	m_DynamicBalanceLevel = std::clamp(
		m_DynamicBalanceInitialLevel,
		m_DynamicBalanceMinLevel,
		m_DynamicBalanceMaxLevel);
	m_DynamicBalanceAppliedEnabled = false;
	m_DynamicBalanceAppliedLevel = m_DynamicBalanceLevel;
	m_DynamicBalanceStageActive = false;
	m_DynamicBalanceShotActive = false;
	m_DynamicBalanceCurrentShotHit = false;
	m_DynamicBalanceStageShots = 0;
	m_DynamicBalanceStageNoHitShots = 0;
	m_DynamicBalanceStageEnemyCount = 0;
	m_DynamicBalanceLastLevelChange = 0;
	m_DynamicBalanceLastHpRatio = 1.0f;
	m_DynamicBalanceLastNoHitRate = 0.0f;
	m_DynamicBalanceLastShotsPerEnemy = 0.0f;
	m_DynamicBalanceLastResult = "not_evaluated";
	m_DynamicBalanceLastReason = "No battle has been evaluated in this run.";
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
	if (!m_DebugMode) LoadPlayerStatusFromJson(); // Refresh the catalog after newly completed achievements.
	ResetPlayerRuntimeStatus();
	ResetDynamicBalanceRunState();
	const std::string effectiveControllerType = !controllerType.empty()
		? controllerType : (m_BalanceAutoPlayEnabled ? "autoplay" : "human");
	m_PersistentProgressEligible = effectiveControllerType == "human" && !m_DebugMode &&
		!m_BalanceAutoPlayEnabled && !m_BalanceValidationEnabled;
	m_ActiveAscension = (!m_DebugMode && !m_BalanceAutoPlayEnabled && !m_BalanceValidationEnabled)
		? m_ProgressionProfile.selectedAscension : 0;
	const int hpPenalty = ProgressionProfile::StartingHpPenalty(m_ActiveAscension);
	m_PlayerRunStatus.maxHp = (std::max)(1, m_PlayerRunStatus.maxHp - hpPenalty);
	m_PlayerRunStatus.currentHp = m_PlayerRunStatus.maxHp;
	m_RestHealRatio = (std::max)(0.05f, m_DefaultRestHealRatio - ProgressionProfile::RestHealPenalty(m_ActiveAscension));
	m_RunStatistics.Reset();
	m_RunActive = true;
	m_IsPaused = false;
	m_PauseConfirmTitle = false;
	m_LastRunResult = RunResultSnapshot{};
	m_PendingShotTelemetry = nlohmann::json::object();
	if (m_BalanceValidationEnabled)
	{
		const std::uint32_t seedCount =
			static_cast<std::uint32_t>(
				(std::max)(std::size_t{ 1 },
					m_BalanceValidationSeeds.size()));
		const std::uint32_t variantCount =
			static_cast<std::uint32_t>(
				(std::max)(std::size_t{ 1 },
					m_BalanceValidationVariants.size()));
		m_BalanceValidationSeedIndex =
			m_BalanceValidationSeeds.empty()
			? 0
			: m_BalanceValidationRunCounter % seedCount;
		m_BalanceValidationVariantIndex =
			(m_BalanceValidationRunCounter / seedCount) % variantCount;
		if (!forcedValidationVariant.empty())
		{
			const auto forcedVariant = std::find_if(
				m_BalanceValidationVariants.begin(),
				m_BalanceValidationVariants.end(),
				[&forcedValidationVariant](
					const BalanceValidationVariant& variant)
				{
					return variant.id == forcedValidationVariant;
				});
			if (forcedVariant != m_BalanceValidationVariants.end())
			{
				m_BalanceValidationVariantIndex =
					static_cast<std::uint32_t>(std::distance(
						m_BalanceValidationVariants.begin(),
						forcedVariant));
			}
		}
		if (!m_BalanceValidationVariants.empty())
		{
			const BalanceValidationVariant& variant =
				m_BalanceValidationVariants[
					m_BalanceValidationVariantIndex];
			m_BalanceValidationCurrentVariantId = variant.id;
			m_BalanceValidationCurrentDisableDynamicBalance =
				variant.disableDynamicBalance;
		}
		else
		{
			m_BalanceValidationCurrentVariantId =
				m_BalanceValidationExperimentId;
			m_BalanceValidationCurrentDisableDynamicBalance =
				m_BalanceValidationDisableDynamicBalance;
		}
		m_RunRandomSeed = m_BalanceValidationSeeds.empty()
			? m_BalanceValidationSeed
			: m_BalanceValidationSeeds[m_BalanceValidationSeedIndex];
		m_BalanceValidationRunCounter++;
		if (m_BalanceValidationCurrentDisableDynamicBalance)
		{
			m_DynamicBalanceEnabled = false;
			m_DynamicBalanceAppliedEnabled = false;
			m_DynamicBalanceLevel = 0;
			m_DynamicBalanceAppliedLevel = 0;
		}
	}
	else if (m_BalanceAutoPlayEnabled)
	{
		m_RunRandomSeed =
			m_AutoRandomSeed + static_cast<std::uint32_t>(m_AutoRunCount);
	}
	else
	{
		m_RunRandomSeed = std::random_device{}();
	}
	if (forcedRandomSeed.has_value())
	{
		m_RunRandomSeed = forcedRandomSeed.value();
		const auto forcedSeed = std::find(
			m_BalanceValidationSeeds.begin(),
			m_BalanceValidationSeeds.end(),
			m_RunRandomSeed);
		if (forcedSeed != m_BalanceValidationSeeds.end())
		{
			m_BalanceValidationSeedIndex =
				static_cast<std::uint32_t>(std::distance(
					m_BalanceValidationSeeds.begin(),
					forcedSeed));
		}
	}
	m_PlayerDeck.Seed(m_RunRandomSeed ^ 0x165667b1u);
	// ResetPlayerRuntimeStatusは通常乱数で初期化されるため、
	// ランシード確定後に山札を再構成し、前後比較を再現可能にする。
	m_PlayerDeck.ResetToDefault();
	m_StageSelectionSeed = m_RunRandomSeed ^ 0x9e3779b9u;
	m_RouteSelectionSeed = m_RunRandomSeed ^ 0x85ebca6bu;
	m_RouteSelectionCounter = 0;
	m_RunProgress.Reset(m_RouteSelectionSeed);
	m_StageSelector.Seed(m_StageSelectionSeed);
	m_AutoRandomEngine.seed(m_RunRandomSeed ^ 0xc2b2ae35u);
	m_PocketRandomEngine.seed(m_RunRandomSeed ^ 0x27d4eb2fu);
	m_RelicRandomEngine.seed(m_RunRandomSeed ^ 0xd3a2646cu);
	if (m_DebugMode) ApplyDebugRunSettings();

	std::vector<BalanceBallSnapshot> deck;
	deck.reserve(static_cast<size_t>(
		m_PlayerDeck.GetRewardTargetCount()));

	for (int index = 0;
		index < m_PlayerDeck.GetRewardTargetCount();
		index++)
	{
		const PlayerBallData* ball =
			m_PlayerDeck.GetRewardTarget(index);
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
		{ "debug_sandbox", m_DebugMode },
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
				{ "enabled", m_BalanceValidationEnabled },
				{ "experiment_id", m_BalanceValidationExperimentId },
				{ "variant_id", m_BalanceValidationCurrentVariantId },
				{ "variant_index", m_BalanceValidationVariantIndex },
				{ "variant_count", m_BalanceValidationVariants.size() },
				{ "maximum_cleared_stages",
					m_BalanceValidationMaximumClearedStages },
				{
					"dynamic_balance_forced_off",
					m_BalanceValidationEnabled &&
					m_BalanceValidationCurrentDisableDynamicBalance
				},
				{ "fixed_stage_schedule", m_BalanceValidationFixedStageSchedule },
				{ "endurance_mode", m_BalanceValidationEnduranceMode },
				{ "seed_suite_index", m_BalanceValidationSeedIndex },
				{ "seed_suite_size", m_BalanceValidationSeeds.size() },
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
		{ "dynamic_balance_enabled_at_start", m_DynamicBalanceEnabled },
		{ "dynamic_balance_level_at_start", m_DynamicBalanceLevel },
		{ "initial_money", m_PlayerRunStatus.money },
		{ "initial_progress", m_PlayerRunStatus.progress },
		{ "ascension", m_ActiveAscension },
		{ "area_goal", kNormalRouteAreaGoal },
		{ "run_map", m_RunProgress.GetMap().Snapshot() },
		{ "run_phase", ToString(m_RunProgress.GetPhase()) },
	};

	BalanceLogger::GetInstance().BeginRun(
		m_PlayerRunStatus.maxHp,
		m_PlayerRunStatus.currentHp,
		deck,
		effectiveControllerType,
		runContext);
}

// Balance Auto Play Configを読み込む。
void Game::LoadBalanceAutoPlayConfig(
	const std::string& filePath)
{
	std::ifstream file(filePath);
	if (!file)
	{
		std::cout
			<< "[BalanceAutoPlay] Config not found: "
			<< filePath << std::endl;
		return;
	}

	try
	{
		nlohmann::json config;
		file >> config;

		m_BalanceAutoPlayEnabled =
			config.value("enabled", false);
		m_AutoRestartAfterGameOver =
			config.value("restart_after_game_over", true);
		m_AutoStopAfterCurrentRunDefault =
			config.value("stop_after_current_run", false);
		m_AutoStopAfterCurrentRunRequested =
			m_BalanceAutoPlayEnabled &&
			m_AutoStopAfterCurrentRunDefault;
		m_AutoDecisionDelayFrames =
			(std::max)(
				1,
				config.value("decision_delay_frames", 20));
		m_AutoMaxRuns =
			(std::max)(0, config.value("max_runs", 0));
		m_AutoMinShotPower =
			std::clamp(
				config.value("min_shot_power", 4.0f),
				1.0f,
				8.0f);
		m_AutoMaxShotPower =
			std::clamp(
				config.value("max_shot_power", 8.0f),
				m_AutoMinShotPower,
				8.0f);
		m_AutoAimJitterDegrees =
			std::clamp(
				config.value("aim_jitter_degrees", 1.5f),
				0.0f,
				15.0f);

		const unsigned int randomSeed =
			config.value("random_seed", 20260727u);
		m_AutoRandomSeed = randomSeed;
		m_AutoRandomEngine.seed(randomSeed);

		std::cout
			<< "[BalanceAutoPlay] "
			<< (m_BalanceAutoPlayEnabled ? "Enabled" : "Disabled")
			<< " / F8 toggles auto play / F9 stops at run end"
			<< std::endl;
	}
	catch (const nlohmann::json::exception& error)
	{
		std::cerr
			<< "[BalanceAutoPlay] Invalid config: "
			<< error.what() << std::endl;
	}
}

// Dynamic Balance Configを読み込む。
void Game::LoadDynamicBalanceConfig(
	const std::string& filePath)
{
	std::ifstream file(filePath);
	if (!file)
	{
		std::cout
			<< "[DynamicBalance] Config not found: "
			<< filePath << std::endl;
		return;
	}

	try
	{
		nlohmann::json config;
		file >> config;

		m_DynamicBalanceConfiguredEnabled =
			config.value(
				"enabled",
				m_DynamicBalanceConfiguredEnabled);
		m_DynamicBalanceEnabled =
			m_DynamicBalanceConfiguredEnabled;
		m_DynamicBalanceMinLevel =
			config.value("minimum_level", m_DynamicBalanceMinLevel);
		m_DynamicBalanceMaxLevel =
			config.value("maximum_level", m_DynamicBalanceMaxLevel);
		if (m_DynamicBalanceMinLevel > m_DynamicBalanceMaxLevel)
		{
			std::swap(
				m_DynamicBalanceMinLevel,
				m_DynamicBalanceMaxLevel);
		}
		m_DynamicBalanceInitialLevel = std::clamp(
			config.value(
				"initial_level",
				m_DynamicBalanceInitialLevel),
			m_DynamicBalanceMinLevel,
			m_DynamicBalanceMaxLevel);
		m_DynamicBalanceLevel = m_DynamicBalanceInitialLevel;
		m_DynamicBalanceHpStep =
			(std::max)(0, config.value(
				"hp_step_per_level",
				m_DynamicBalanceHpStep));
		m_DynamicBalanceAttackStep =
			(std::max)(0, config.value(
				"attack_step",
				m_DynamicBalanceAttackStep));
		m_DynamicBalanceLevelsPerAttackStep =
			(std::max)(1, config.value(
				"levels_per_attack_step",
				m_DynamicBalanceLevelsPerAttackStep));
		m_DynamicBalancePositiveAttackScalingEnabled =
			config.value(
				"positive_attack_scaling_enabled",
				m_DynamicBalancePositiveAttackScalingEnabled);
		const nlohmann::json progression = config.value(
			"progression_scaling",
			nlohmann::json::object());
		if (progression.is_object())
		{
			m_ProgressionScalingEnabled = progression.value(
				"enabled",
				m_ProgressionScalingEnabled);
			m_ProgressionHpStart = (std::max)(
				1,
				progression.value(
					"hp_start_progress",
					m_ProgressionHpStart));
			m_ProgressionHpInterval = (std::max)(
				1,
				progression.value(
					"hp_interval",
					m_ProgressionHpInterval));
			m_ProgressionHpStep = (std::max)(
				0,
				progression.value(
					"hp_step",
					m_ProgressionHpStep));
			m_ProgressionHpMaximumDelta = (std::max)(
				0,
				progression.value(
					"maximum_hp_delta",
					m_ProgressionHpMaximumDelta));
			m_ProgressionAttackStart = (std::max)(
				1,
				progression.value(
					"attack_start_progress",
					m_ProgressionAttackStart));
			m_ProgressionAttackInterval = (std::max)(
				1,
				progression.value(
					"attack_interval",
					m_ProgressionAttackInterval));
			m_ProgressionAttackStep = (std::max)(
				0,
				progression.value(
					"attack_step",
					m_ProgressionAttackStep));
			m_ProgressionAttackMaximumDelta = (std::max)(
				0,
				progression.value(
					"maximum_attack_delta",
					m_ProgressionAttackMaximumDelta));
		}
		m_DynamicBalanceMinEnemyHp =
			(std::max)(1, config.value(
				"minimum_enemy_hp",
				m_DynamicBalanceMinEnemyHp));
		m_DynamicBalanceMaxEnemyHp =
			(std::max)(
				m_DynamicBalanceMinEnemyHp,
				config.value(
					"maximum_enemy_hp",
					m_DynamicBalanceMaxEnemyHp));
		m_DynamicBalanceMinEnemyAttack =
			(std::max)(0, config.value(
				"minimum_enemy_attack",
				m_DynamicBalanceMinEnemyAttack));
		m_DynamicBalanceMaxEnemyAttack =
			(std::max)(
				m_DynamicBalanceMinEnemyAttack,
				config.value(
					"maximum_enemy_attack",
					m_DynamicBalanceMaxEnemyAttack));
		m_DynamicBalanceStrongHpRatio = std::clamp(
			config.value(
				"strong_clear_hp_ratio",
				m_DynamicBalanceStrongHpRatio),
			0.0f,
			1.0f);
		m_DynamicBalanceWeakHpRatio = std::clamp(
			config.value(
				"weak_clear_hp_ratio",
				m_DynamicBalanceWeakHpRatio),
			0.0f,
			1.0f);
		m_DynamicBalanceStrongNoHitRate = std::clamp(
			config.value(
				"strong_no_hit_rate",
				m_DynamicBalanceStrongNoHitRate),
			0.0f,
			1.0f);
		m_DynamicBalanceWeakNoHitRate = std::clamp(
			config.value(
				"weak_no_hit_rate",
				m_DynamicBalanceWeakNoHitRate),
			0.0f,
			1.0f);
		m_DynamicBalanceTargetShotsPerEnemy =
			(std::max)(
				0.1f,
				config.value(
					"target_shots_per_enemy",
					m_DynamicBalanceTargetShotsPerEnemy));
		m_DynamicBalanceWeakShotMultiplier =
			(std::max)(
				1.0f,
				config.value(
					"weak_shot_multiplier",
					m_DynamicBalanceWeakShotMultiplier));

		std::cout
			<< "[DynamicBalance] "
			<< (m_DynamicBalanceEnabled ? "Enabled" : "Disabled")
			<< " / Level=" << m_DynamicBalanceLevel
			<< " / Range=" << m_DynamicBalanceMinLevel
			<< ".." << m_DynamicBalanceMaxLevel
			<< std::endl;
	}
	catch (const nlohmann::json::exception& error)
	{
		std::cerr
			<< "[DynamicBalance] Invalid config: "
			<< error.what() << std::endl;
	}
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

// Balance Validation Configを読み込む。
void Game::LoadBalanceValidationConfig(
	const std::string& filePath)
{
	std::ifstream file(filePath);
	if (!file)
	{
		std::cout << "[BalanceValidation] Config not found: "
			<< filePath << std::endl;
		return;
	}

	try
	{
		nlohmann::json config;
		file >> config;
		m_BalanceValidationEnabled =
			config.value("enabled", false);
		m_BalanceValidationDisableDynamicBalance =
			config.value("disable_dynamic_balance", true);
		m_BalanceValidationFixedStageSchedule =
			config.value("fixed_stage_schedule", true);
		m_BalanceValidationEnduranceMode =
			config.value("endurance_mode", false);
		m_BalanceValidationSeed =
			config.value("random_seed", 20260807u);
		m_BalanceValidationSeeds.clear();
		if (config.contains("random_seeds") &&
			config["random_seeds"].is_array())
		{
			for (const nlohmann::json& seed : config["random_seeds"])
			{
				if (seed.is_number_unsigned() || seed.is_number_integer())
				{
					const long long value = seed.get<long long>();
					if (value >= 0 && value <= 0xffffffffll)
					{
						m_BalanceValidationSeeds.push_back(
							static_cast<std::uint32_t>(value));
					}
				}
			}
		}
		if (m_BalanceValidationSeeds.empty())
		{
			m_BalanceValidationSeeds.push_back(
				m_BalanceValidationSeed);
		}
		m_BalanceValidationExperimentId = config.value(
			"experiment_id",
			std::string("fixed_baseline"));
		m_BalanceValidationMaximumClearedStages = (std::max)(
			0,
			config.value(
				"maximum_cleared_stages_per_run",
				m_BalanceValidationMaximumClearedStages));
		m_BalanceValidationVariants.clear();
		if (config.contains("variants") &&
			config["variants"].is_array())
		{
			for (const nlohmann::json& variant : config["variants"])
			{
				if (!variant.is_object())
				{
					continue;
				}
				const std::string id = variant.value(
					"id",
					std::string());
				if (id.empty())
				{
					continue;
				}
				m_BalanceValidationVariants.push_back({
					id,
					variant.value(
						"disable_dynamic_balance",
						m_BalanceValidationDisableDynamicBalance),
				});
			}
		}
		if (m_BalanceValidationVariants.empty())
		{
			m_BalanceValidationVariants.push_back({
				m_BalanceValidationExperimentId,
				m_BalanceValidationDisableDynamicBalance,
			});
		}
		m_BalanceValidationVariantIndex = 0;
		m_BalanceValidationCurrentVariantId =
			m_BalanceValidationVariants.front().id;
		m_BalanceValidationCurrentDisableDynamicBalance =
			m_BalanceValidationVariants.front().disableDynamicBalance;
		if (m_BalanceValidationEnabled &&
			config.contains("baseline_profile") &&
			config["baseline_profile"].is_string())
		{
			const std::string requestedProfile =
				config["baseline_profile"].get<std::string>();
			if (requestedProfile != m_BaselineDifficultyProfile)
			{
				std::cout
					<< "[BalanceValidation] baseline_profile is "
					<< requestedProfile
					<< "; set the same selected_profile in "
					<< "difficulty_profiles.json to apply it."
					<< std::endl;
			}
		}
		std::cout << "[BalanceValidation] "
			<< (m_BalanceValidationEnabled ? "Enabled" : "Disabled")
			<< " / Seed=" << m_BalanceValidationSeed
			<< " / Experiment=" << m_BalanceValidationExperimentId
			<< " / Variants=" << m_BalanceValidationVariants.size()
			<< std::endl;
	}
	catch (const nlohmann::json::exception& error)
	{
		std::cerr << "[BalanceValidation] Invalid config: "
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
		m_PlayerPocketDamageRatio = std::clamp(
			config.value("player_max_hp_damage_ratio",
				m_PlayerPocketDamageRatio),
			0.0f,
			1.0f);
		const nlohmann::json finishers = config.value(
			"enemy_finisher_hp_ratios",
			nlohmann::json::object());
		m_NormalPocketFinisherRatio = std::clamp(
			finishers.value("normal", m_NormalPocketFinisherRatio),
			0.0f,
			1.0f);
		m_MidBossPocketFinisherRatio = std::clamp(
			finishers.value("midboss", m_MidBossPocketFinisherRatio),
			0.0f,
			1.0f);
		m_BossPocketFinisherRatio = std::clamp(
			finishers.value("boss", m_BossPocketFinisherRatio),
			0.0f,
			1.0f);
		const nlohmann::json playerReturn = config.value(
			"player_return_region",
			nlohmann::json::object());
		m_PlayerPocketReturnHalfWidth = (std::max)(
			0.0f,
			playerReturn.value(
				"half_width", m_PlayerPocketReturnHalfWidth));
		m_PlayerPocketReturnHalfDepth = (std::max)(
			0.0f,
			playerReturn.value(
				"half_depth", m_PlayerPocketReturnHalfDepth));
		const nlohmann::json enemyReturn = config.value(
			"enemy_return",
			nlohmann::json::object());
		const nlohmann::json enemyReturnPosition = enemyReturn.value(
			"position",
			nlohmann::json::object());
		m_EnemyPocketReturnX = enemyReturnPosition.value(
			"x", m_EnemyPocketReturnX);
		m_EnemyPocketReturnTopEdgeOffset = (std::max)(
			0.0f,
			enemyReturnPosition.value(
				"z_from_top_edge",
				m_EnemyPocketReturnTopEdgeOffset));
	}
	catch (const nlohmann::json::exception& error)
	{
		std::cerr << "[PocketRules] Invalid config: "
			<< error.what() << std::endl;
	}
}

// Dynamic Balance Shotを終了する。
void Game::FinishDynamicBalanceShot()
{
	if (!m_DynamicBalanceStageActive ||
		!m_DynamicBalanceShotActive)
	{
		return;
	}

	if (!m_DynamicBalanceCurrentShotHit)
	{
		m_DynamicBalanceStageNoHitShots++;
	}
	m_DynamicBalanceShotActive = false;
}

// Dynamic Balance Stageを評価する。
void Game::EvaluateDynamicBalanceStage(bool cleared)
{
	if (!m_DynamicBalanceStageActive)
	{
		return;
	}

	FinishDynamicBalanceShot();

	const int safeEnemyCount =
		(std::max)(1, m_DynamicBalanceStageEnemyCount);
	const float targetShots =
		m_DynamicBalanceTargetShotsPerEnemy *
		static_cast<float>(safeEnemyCount);
	m_DynamicBalanceLastHpRatio =
		m_PlayerRunStatus.maxHp <= 0
			? 0.0f
			: std::clamp(
				static_cast<float>(m_PlayerRunStatus.currentHp) /
					static_cast<float>(m_PlayerRunStatus.maxHp),
				0.0f,
				1.0f);
	m_DynamicBalanceLastNoHitRate =
		m_DynamicBalanceStageShots <= 0
			? 0.0f
			: static_cast<float>(
				m_DynamicBalanceStageNoHitShots) /
				static_cast<float>(m_DynamicBalanceStageShots);
	m_DynamicBalanceLastShotsPerEnemy =
		static_cast<float>(m_DynamicBalanceStageShots) /
		static_cast<float>(safeEnemyCount);
	m_DynamicBalanceLastResult =
		cleared ? "clear" : "game_over";
	m_DynamicBalanceLastLevelChange = 0;

	int requestedChange = 0;
	if (!m_DynamicBalanceEnabled)
	{
		m_DynamicBalanceLastReason =
			"Automatic adjustment is disabled.";
	}
	else if (!cleared)
	{
		requestedChange = -1;
		m_DynamicBalanceLastReason =
			"Game over: reduce the next battle difficulty.";
	}
	else
	{
		const bool strongClear =
			m_DynamicBalanceLastHpRatio >=
				m_DynamicBalanceStrongHpRatio &&
			m_DynamicBalanceLastNoHitRate <=
				m_DynamicBalanceStrongNoHitRate &&
			static_cast<float>(m_DynamicBalanceStageShots) <=
				targetShots;
		const bool weakClear =
			m_DynamicBalanceLastHpRatio <=
				m_DynamicBalanceWeakHpRatio ||
			m_DynamicBalanceLastNoHitRate >=
				m_DynamicBalanceWeakNoHitRate ||
			static_cast<float>(m_DynamicBalanceStageShots) >
				targetShots *
				m_DynamicBalanceWeakShotMultiplier;

		if (strongClear)
		{
			requestedChange = 1;
			m_DynamicBalanceLastReason =
				m_DynamicBalanceLevel >= m_DynamicBalanceMaxLevel
				? "Strong clear: assist mode is already at baseline."
				: "Strong clear: reduce assistance for the next battle.";
		}
		else if (weakClear)
		{
			requestedChange = -1;
			m_DynamicBalanceLastReason =
				"Struggling clear: reduce the next battle difficulty.";
		}
		else
		{
			m_DynamicBalanceLastReason =
				"Performance is inside the target range.";
		}
	}

	const int previousLevel = m_DynamicBalanceLevel;
	m_DynamicBalanceLevel = std::clamp(
		m_DynamicBalanceLevel + requestedChange,
		m_DynamicBalanceMinLevel,
		m_DynamicBalanceMaxLevel);
	m_DynamicBalanceLastLevelChange =
		m_DynamicBalanceLevel - previousLevel;
	m_DynamicBalanceStageActive = false;

	std::cout
		<< "[DynamicBalance] Result="
		<< m_DynamicBalanceLastResult
		<< " HP=" << m_DynamicBalanceLastHpRatio
		<< " NoHit=" << m_DynamicBalanceLastNoHitRate
		<< " ShotsPerEnemy="
		<< m_DynamicBalanceLastShotsPerEnemy
		<< " Level=" << previousLevel
		<< "->" << m_DynamicBalanceLevel
		<< " Reason=" << m_DynamicBalanceLastReason
		<< std::endl;
}
