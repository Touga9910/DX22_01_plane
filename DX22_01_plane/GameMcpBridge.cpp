#include "GameMcpBridge.h"
#include "BallStatusJson.h"
#include "GameObject.h"
#include "json/json.hpp"

#include "EnemyBall.h"
#include "BreakBall.h"
#include "Game.h"
#include "GameRuntime.h"
#include "GameRuntimeState.h"
#include "GameWorld.h"
#include "GamePersistenceRuntime.h"
#include "GameRunDefaults.h"
#include "GameBattleState.h"
#include "GameBalanceRuntime.h"
#include "PlayerBallData.h"
#include "PlayerRunStatus.h"
#include "RunController.h"
#include "GameDebugController.h"
#include "GameStageOverrideState.h"
#include "GameMcpCommands.h"
#include "GameMcpState.h"
#include "RunProgressController.h"
#include "BattleScene.h"
#include "ResultScene.h"
#include "RestSiteScene.h"
#include "ShopScene.h"
#include "StageSelectScene.h"
#include "TitleScene.h"
#include "BallPhysicsWorld.h"
#include "PlayerBall.h"
#include "Pocket.h"
#include "StageDataLoader.h"
#include "TableFrame.h"
#include "TableConfig.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace
{
	constexpr int kShopRemoveCost = 15;
	constexpr int kExtraRewardMoney = 10;
	constexpr std::size_t kMaximumMcpStageEnemyCount = 12;

	std::int64_t UnixTimeMilliseconds()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
	}

	bool ReadJson(
		const std::filesystem::path& path,
		nlohmann::json& output)
	{
		std::ifstream file(path);
		if (!file)
		{
			return false;
		}

		try
		{
			file >> output;
			return true;
		}
		catch (const nlohmann::json::exception&)
		{
			return false;
		}
	}

	bool WriteJsonAtomically(
		const std::filesystem::path& path,
		const nlohmann::json& value)
	{
		std::error_code error;
		std::filesystem::create_directories(
			path.parent_path(),
			error);
		if (error)
		{
			return false;
		}

		const std::filesystem::path temporaryPath =
			path.string() + ".tmp";
		{
			std::ofstream file(
				temporaryPath,
				std::ios::trunc);
			if (!file)
			{
				return false;
			}
			file << value.dump(2);
			file.flush();
			if (!file)
			{
				return false;
			}
		}

#if defined(_WIN32)
		constexpr DWORD replaceFlags =
			MOVEFILE_REPLACE_EXISTING |
			MOVEFILE_WRITE_THROUGH;
		for (int attempt = 0; attempt < 5; attempt++)
		{
			if (MoveFileExW(
				temporaryPath.c_str(),
				path.c_str(),
				replaceFlags))
			{
				return true;
			}

			const DWORD replaceError = GetLastError();
			if (replaceError != ERROR_ACCESS_DENIED &&
				replaceError != ERROR_SHARING_VIOLATION &&
				replaceError != ERROR_LOCK_VIOLATION)
			{
				break;
			}
			std::this_thread::sleep_for(
				std::chrono::milliseconds(1 << attempt));
		}
#else
		std::filesystem::rename(
			temporaryPath,
			path,
			error);
		if (!error)
		{
			return true;
		}
#endif

		std::filesystem::remove(temporaryPath, error);
		return false;
	}

	const char* GetGameStateName(const Game& game)
	{
		if (game.IsClearRewardActive())
		{
			return "clear_reward";
		}

		if (game.GetLastBattleResult() == BattleResult::Defeat &&
			game.GetCurrentSceneType() == SceneType::Result)
		{
			return "game_over";
		}

		switch (game.GetBattleState())
		{
		case BattleState::AimingDirection:
			return "aiming_direction";
		case BattleState::AimingPower:
			return "aiming_power";
		case BattleState::BallsMoving:
			return "balls_moving";
		case BattleState::EnemyAttack:
			return "enemy_attack";
		case BattleState::TurnEnd:
			return "turn_end";
		case BattleState::Finished:
			return "finished";
		case BattleState::Inactive:
		default:
			return "inactive";
		}
	}


	const char* GetSceneName(const Scene* scene)
	{
		if (dynamic_cast<const TitleScene*>(scene) != nullptr)
		{
			return "title";
		}
		if (dynamic_cast<const StageSelectScene*>(scene) != nullptr)
		{
			return "stage_select";
		}
		if (dynamic_cast<const BattleScene*>(scene) != nullptr)
		{
			return "battle";
		}
		if (dynamic_cast<const RestSiteScene*>(scene) != nullptr)
		{
			return "rest_site";
		}
		if (dynamic_cast<const ShopScene*>(scene) != nullptr)
		{
			return "shop";
		}
		if (dynamic_cast<const ResultScene*>(scene) != nullptr)
		{
			return "result";
		}
		return "unknown";
	}

	nlohmann::json VectorToJson(
		const DirectX::SimpleMath::Vector3& vector)
	{
		return {
			{ "x", vector.x },
			{ "y", vector.y },
			{ "z", vector.z },
		};
	}

	const char* StageTypeToMcpString(StageType stageType)
	{
		switch (stageType)
		{
		case StageType::MidBoss:
			return "midboss";
		case StageType::Boss:
			return "boss";
		default:
			return "normal";
		}
	}

	nlohmann::json StageDataToJson(const StageData& stage)
	{
		nlohmann::json enemies = nlohmann::json::array();
		nlohmann::json breakBalls = nlohmann::json::array();
		for (const EnemySpawnData& spawn : stage.enemies)
		{
			enemies.push_back({
				{ "enemy_id", spawn.enemyId },
				{ "position", VectorToJson(spawn.position) },
				{ "max_hp", spawn.enemyData.maxHp },
				{ "frontal_damage_multiplier", spawn.enemyData.frontalDamageMultiplier },
				{ "pocket_damage_ratio", spawn.enemyData.pocketDamageRatio },
				{ "attack", spawn.enemyData.status.attack },
				{ "radius", spawn.enemyData.status.radius },
			});
		}
		const auto positions = stage.hasBreakBallLayout
			? stage.breakBallPositions
			: (stage.stageType == StageType::Boss
				? DefaultBossBreakBallPositions()
				: std::vector<DirectX::SimpleMath::Vector3>{});
		for (const auto& position : positions)
			breakBalls.push_back(VectorToJson(position));

		return {
			{ "layout_id", stage.id },
			{ "stage_type", StageTypeToMcpString(stage.stageType) },
			{ "difficulty", stage.difficulty },
			{ "par", stage.par },
			{ "enemies", std::move(enemies) },
			{ "break_balls", std::move(breakBalls) },
		};
	}

	bool IsValidLayoutId(const std::string& layoutId)
	{
		if (layoutId.empty() || layoutId.size() > 64)
		{
			return false;
		}
		return std::all_of(
			layoutId.begin(),
			layoutId.end(),
			[](unsigned char character)
			{
				return std::isalnum(character) != 0 ||
					character == '_' ||
					character == '-' ||
					character == '.';
			});
	}

	nlohmann::json BallDataToJson(
		const PlayerBallData& ball,
		int index)
	{
		return {
			{ "index", index },
			{ "definition_id", ball.definitionId },
			{ "category", BallCategoryId(ball.category) },
			{ "instance_id", ball.instanceId },
			{ "upgrade_level", ball.upgradeLevel },
			{ "can_upgrade", ball.CanUpgrade() },
			{ "next_upgrade", ball.CanUpgrade() ? WritePlayerBallStatus(ball.upgradeTable[ball.upgradeLevel]) : nlohmann::json(nullptr) },
			{ "status", {
				{ "attack", ball.status.attack },
				{ "mass", ball.status.mass },
				{ "radius", ball.status.radius },
				{ "restitution", ball.status.restitution },
				{ "friction", ball.status.friction },
				{ "knockbackTransfer", ball.status.knockbackTransfer },
				{ "pierceMaxUses", ball.status.pierceMaxUses },
				{ "pierceSpeedRetention", ball.status.pierceSpeedRetention },
				{ "anchorBrakeMultiplier", ball.status.anchorBrakeMultiplier },
				{ "anchorStopSpeedSquared", ball.status.anchorStopSpeedSquared },
				{ "anchorKnockbackImmune", ball.status.anchorKnockbackImmune },
				{ "heavySetupStackConsume", ball.status.heavySetupStackConsume },
				{ "heavyEnemyCollisionMultiplier", ball.status.heavyEnemyCollisionMultiplier },
				{ "lightweightKnockbackMultiplier", ball.status.lightweightKnockbackMultiplier },
				{ "pierceStackConsumeLimit", ball.status.pierceStackConsumeLimit },
				{ "cushionReflectionLimit", ball.status.cushionReflectionLimit },
				{ "cushionConsumptionLimit", ball.status.cushionConsumptionLimit },
				{ "cushionReflectionSpeedMultiplier", ball.status.cushionReflectionSpeedMultiplier },
				{ "reflectionSetupAmount", ball.status.reflectionSetupAmount },
				{ "ricochetStackConsume", ball.status.ricochetStackConsume },
				{ "pinballCollisionsPerStage", ball.status.pinballCollisionsPerStage },
				{ "pinballSpeedRetention", ball.status.pinballSpeedRetention },
				{ "anchorPlayerStacksPerBonus", ball.status.anchorPlayerStacksPerBonus },
				{ "anchorEnemyConsumeLimit", ball.status.anchorEnemyConsumeLimit },
				{ "nextAnchorGrantBonus", ball.status.nextAnchorGrantBonus },
				{ "stopShieldAmount", ball.status.stopShieldAmount },
				{ "chainImpactRadius", ball.status.chainImpactRadius },
				{ "pierce", ball.status.abilities.pierce },
				{ "refractAfterPierce", ball.status.abilities.refractAfterPierce },
				{ "split", ball.status.abilities.split },
				{ "anchor", ball.status.abilities.anchor },
				{ "heavyDistanceStacks", ball.status.abilities.heavyDistanceStacks },
				{ "lightweight", ball.status.abilities.lightweight },
				{ "reflectionSetup", ball.status.abilities.reflectionSetup },
				{ "returnToShotStart", ball.status.abilities.returnToShotStart },
			} },
		};
	}

	int FindDeckBallIndex(
		const Game& game,
		std::uint64_t instanceId)
	{
		for (int index = 0;
			index < game.GetDeckBallCount();
			index++)
		{
			const PlayerBallData* ball =
				game.GetDeckBall(index);
			if (ball != nullptr &&
				ball->instanceId == instanceId)
			{
				return index;
			}
		}
		return -1;
	}

	StageType ParseStageType(const std::string& value)
	{
		if (value == "midboss")
		{
			return StageType::MidBoss;
		}
		if (value == "boss")
		{
			return StageType::Boss;
		}
		return StageType::Normal;
	}

	nlohmann::json CommandResult(
		bool ok,
		const std::string& message)
	{
		return {
			{ "ok", ok },
			{ "message", message },
		};
	}
}

GameMcpReadSnapshot GameMcpReadView::Capture(const Game& game)
{
	GameMcpReadSnapshot snapshot;
	const GameBalanceReadSnapshot balance =
		game.m_BalanceRuntime->CaptureReadSnapshot(
			game.m_RunController->Status().progress,
			game.m_RunController->Progress().GetClearedBattleCount());
	snapshot.autoplayEnabled = balance.automation.enabled;
	snapshot.autoplayStopAfterCurrentRun =
		balance.automation.stopAfterCurrentRunRequested;
	snapshot.autoplayStopAfterCurrentRunDefault =
		balance.automation.stopAfterCurrentRunDefault;

	const GameRuntimeState::PhysicsTiming physics =
		game.m_RuntimeState->CapturePhysicsTiming();
	snapshot.physics.substepsLastTick = physics.substepsLastTick;
	snapshot.physics.substepLimitCount = physics.substepLimitCount;
	snapshot.physics.tick = physics.tick;
	snapshot.physics.stepsLastFrame = physics.stepsLastFrame;
	snapshot.physics.droppedSeconds = physics.droppedSeconds;
	snapshot.restHealRatio = game.m_RunController->RestHealRatio();

	const ProgressionProfile& progression =
		game.m_PersistenceRuntime->Profile();
	snapshot.metaProgression.activeAscension =
		game.m_PersistenceRuntime->ActiveAscension();
	snapshot.metaProgression.selectedAscension = progression.selectedAscension;
	snapshot.metaProgression.highestUnlockedAscension =
		progression.highestUnlockedAscension;
	snapshot.metaProgression.totalRuns = progression.totalRuns;
	snapshot.metaProgression.totalClears = progression.totalClears;
	snapshot.metaProgression.persistentRewardsEligible =
		game.m_PersistenceRuntime->PersistentProgressEligible();
	snapshot.metaProgression.achievementUnlocked.reserve(
		AchievementCatalog.size());
	for (const AchievementDefinition& achievement : AchievementCatalog)
	{
		snapshot.metaProgression.achievementUnlocked.push_back(
			progression.IsAchievementUnlocked(achievement.id));
	}
	constexpr const char* unlockableBallIds[]{
		"player_pierce",
		"player_refractive_pierce",
		"player_trace_driver",
		"player_pierce_finisher",
		"player_bounce",
		"player_cushion_charge",
		"player_ricochet_finisher",
		"player_anchor",
		"player_stop_shield",
		"player_anchor_finisher",
	};
	for (const char* ballId : unlockableBallIds)
	{
		if (progression.IsBallUnlocked(ballId))
		{
			snapshot.metaProgression.unlockedBallIds.emplace_back(ballId);
		}
	}
	snapshot.metaProgression.relicUnlocked.reserve(game.GetRelicCount());
	for (int index = 0; index < game.GetRelicCount(); ++index)
	{
		const RelicDefinition* relic = game.GetRelic(index);
		snapshot.metaProgression.relicUnlocked.push_back(
			relic != nullptr && progression.IsRelicUnlocked(relic->type));
	}

	if (game.m_BattleState != nullptr)
	{
		const BattleController& controller = game.m_BattleState->controller;
		const BattlePocketRules& rules = controller.PocketRules();
		snapshot.pocketRules.playerDamageRatio = rules.playerDamageRatio;
		snapshot.pocketRules.playerReturnHalfWidth = rules.playerReturnHalfWidth;
		snapshot.pocketRules.playerReturnHalfDepth = rules.playerReturnHalfDepth;
		snapshot.pocketRules.enemyReturnX = rules.enemyReturnX;
		snapshot.pocketRules.enemyReturnTopEdgeOffset =
			rules.enemyReturnTopEdgeOffset;
		snapshot.pocketRules.normalFinisherRatio = rules.normalFinisherRatio;
		snapshot.pocketRules.midBossFinisherRatio = rules.midBossFinisherRatio;
		snapshot.pocketRules.bossFinisherRatio = rules.bossFinisherRatio;
		snapshot.pocketRules.stageType = controller.GetStageType();
		snapshot.pocketRules.returnQueueSize = controller.GetPocketQueueSize();
	}

	snapshot.queuedStageOverride.reset();
	if (const StageData* queued = game.m_StageOverrideState->Queued())
	{
		snapshot.queuedStageOverride = *queued;
	}
	snapshot.currentStageOverride.reset();
	if (const StageData* current = game.m_StageOverrideState->Current())
	{
		snapshot.currentStageOverride = *current;
	}
	snapshot.runStatistics =
		game.m_PersistenceRuntime->Statistics().GetState();
	snapshot.selectedOfferIndex = game.GetSelectedBallOfferIndex();
	snapshot.selectedHoldIndex = game.GetHeldBallOfferIndex();
	snapshot.pendingBallAdjustments = balance.pendingBallAdjustments;

	snapshot.progressionScaling.enabled =
		balance.progressionScalingEnabled;
	snapshot.progressionScaling.currentProgress = balance.currentProgress;
	snapshot.progressionScaling.nextEnemyHpDelta =
		balance.nextEnemyHpDelta;
	snapshot.progressionScaling.hpStartProgress = balance.progressionHpStart;
	snapshot.progressionScaling.hpInterval = balance.progressionHpInterval;
	snapshot.progressionScaling.hpStep = balance.progressionHpStep;
	snapshot.progressionScaling.maximumHpDelta =
		balance.progressionHpMaximumDelta;
	snapshot.progressionScaling.nextEnemyAttackDelta =
		balance.nextEnemyAttackDelta;
	snapshot.progressionScaling.attackStartProgress =
		balance.progressionAttackStart;
	snapshot.progressionScaling.attackInterval =
		balance.progressionAttackInterval;
	snapshot.progressionScaling.attackStep = balance.progressionAttackStep;
	snapshot.progressionScaling.maximumAttackDelta =
		balance.progressionAttackMaximumDelta;
	snapshot.baselineDifficultyProfile = balance.baselineDifficultyProfile;
	snapshot.baselineEnemyHpMultiplier = balance.baselineEnemyHpMultiplier;
	snapshot.baselineEnemyAttackDelta = balance.baselineEnemyAttackDelta;

	snapshot.balanceValidation.enabled = balance.validationEnabled;
	snapshot.balanceValidation.experimentId = balance.validationExperimentId;
	snapshot.balanceValidation.variantId = balance.validationVariantId;
	snapshot.balanceValidation.variantIndex = balance.validationVariantIndex;
	snapshot.balanceValidation.variantCount = balance.validationVariantCount;
	snapshot.balanceValidation.runSeed = balance.runRandomSeed;
	snapshot.balanceValidation.stageSelectionSeed = balance.stageSelectionSeed;
	snapshot.balanceValidation.routeSelectionSeed = balance.routeSelectionSeed;
	snapshot.balanceValidation.seedSuiteIndex = balance.validationSeedIndex;
	snapshot.balanceValidation.seedSuiteSize = balance.validationSeedCount;
	snapshot.balanceValidation.maximumClearedStages =
		balance.validationMaximumClearedStages;
	snapshot.balanceValidation.enduranceMode =
		balance.validationEnduranceMode;
	snapshot.balanceValidation.clearedStageCount =
		balance.clearedStageCount;

	const GameDebugController& debug = *game.m_DebugController;
	snapshot.debug.active = debug.IsActive();
	snapshot.debug.editorOpen = debug.IsEditorOpen();
	snapshot.debug.finished = debug.IsBattleFinished();
	if (!debug.GetStageEditor().draft.is_null())
	{
		snapshot.debug.stageEditor = debug.GetStageEditor().Snapshot(
			debug.GetEnemyCatalog(), debug.StageEditorPlayerRadius());
		snapshot.debug.stageEditor["open"] = debug.IsEditorOpen();
	}
	return snapshot;
}

nlohmann::json GameMcpCommands::InspectStageLayout(
	const Game& game,
	const nlohmann::json& layout)
{
	const GameDebugController& debug = *game.m_DebugController;
	return StageLayoutEditor::Inspect(
		layout,
		debug.GetEnemyCatalog(),
		debug.StageEditorPlayerRadius());
}

void GameMcpCommands::ProposeStageLayout(
	Game& game,
	const nlohmann::json& layout,
	std::uint64_t expectedRevision)
{
	GameDebugController& debug = *game.m_DebugController;
	debug.GetStageEditor().Propose(
		layout,
		expectedRevision,
		debug.GetEnemyCatalog(),
		debug.StageEditorPlayerRadius());
}

float GameMcpCommands::GetDefaultPlayerRadius(const Game& game)
{
	return game.m_RunDefaults->PlayerRadius();
}

void GameMcpCommands::QueueNextStageOverride(Game& game, StageData stage)
{
	game.m_StageOverrideState->Queue(std::move(stage));
}

void GameMcpCommands::ClearNextStageOverride(Game& game)
{
	game.m_StageOverrideState->ClearQueued();
}

void GameMcpCommands::PrunePendingBallAdjustments(Game& game)
{
	game.PruneBalanceAutoPendingBalls();
}

void GameMcpCommands::DisableAutoPlayForExternalControl(Game& game)
{
	game.m_BalanceRuntime->SetAutoPlayEnabled(false);
}

bool GameMcpCommands::HasValidationVariant(
	const Game& game,
	const std::string& variantId)
{
	return game.m_BalanceRuntime->HasValidationVariant(variantId);
}

bool GameMcpCommands::ApplyExtraMoneyReward(Game& game, int amount)
{
	if (!game.IsClearRewardActive() ||
		game.IsClearRewardChosen() ||
		game.IsMidBossRelicSelectionActive() ||
		amount <= 0)
	{
		return false;
	}
	game.m_RunController->AddMoney(amount);
	return true;
}

bool GameMcpBridge::Initialize(
	Game& game,
	const std::string& configPath)
{
	nlohmann::json config;
	if (!ReadJson(configPath, config))
	{
		std::cout
			<< "[GameMcpBridge] Config not found or invalid: "
			<< configPath << std::endl;
		return false;
	}

	try
	{
		m_Enabled = config.value("enabled", false);
		m_AllowWriteActions =
			config.value("allow_write_actions", true);
		m_PublishIntervalFrames =
			(std::max)(
				1,
				config.value(
					"state_publish_interval_frames",
					60));
		m_CommandPollIntervalFrames =
			(std::max)(
				1,
				config.value(
					"command_poll_interval_frames",
					6));
		m_BridgeDirectory =
			config.value(
				"bridge_directory",
				std::string("runtime/game_mcp"));
	}
	catch (const nlohmann::json::exception& error)
	{
		std::cerr
			<< "[GameMcpBridge] Invalid config: "
			<< error.what() << std::endl;
		m_Enabled = false;
		return false;
	}

	if (!m_Enabled)
	{
		std::cout << "[GameMcpBridge] Disabled" << std::endl;
		return false;
	}

	m_StageEnemyIds.clear();
	for (const EnemyData& enemy :
		StageDataLoader::LoadEnemyDefinitions(
			"assets/data/enemy_data.json"))
	{
		m_StageEnemyIds.push_back(enemy.id);
	}

	std::error_code error;
	std::filesystem::create_directories(
		m_BridgeDirectory,
		error);
	if (error)
	{
		std::cerr
			<< "[GameMcpBridge] Cannot create bridge directory: "
			<< error.message() << std::endl;
		m_Enabled = false;
		return false;
	}

	std::filesystem::remove(GetCommandPath(), error);
	m_FramesUntilPublish = 0;
	m_FramesUntilCommandPoll = 0;
	PublishState(game, true);
	m_FramesUntilPublish = m_PublishIntervalFrames;

	std::cout
		<< "[GameMcpBridge] Enabled at "
		<< m_BridgeDirectory.string()
		<< std::endl;
	return true;
}

void GameMcpBridge::Update(Game& game)
{
	if (!m_Enabled)
	{
		return;
	}

	m_FramesUntilCommandPoll--;
	if (m_FramesUntilCommandPoll <= 0)
	{
		ProcessPendingCommand(game);
		m_FramesUntilCommandPoll = m_CommandPollIntervalFrames;
	}

	m_FramesUntilPublish--;
	if (m_FramesUntilPublish <= 0)
	{
		PublishState(game, true);
		m_FramesUntilPublish = m_PublishIntervalFrames;
	}
}

void GameMcpBridge::Shutdown(Game& game)
{
	if (!m_Enabled)
	{
		return;
	}

	PublishState(game, false);
	m_Enabled = false;
}

std::filesystem::path GameMcpBridge::GetStatePath() const
{
	return m_BridgeDirectory / "game_state.json";
}

std::filesystem::path GameMcpBridge::GetCommandPath() const
{
	return m_BridgeDirectory / "pending_command.json";
}

std::filesystem::path GameMcpBridge::GetResultPath() const
{
	return m_BridgeDirectory / "last_result.json";
}

nlohmann::json GameMcpBridge::BuildState(
	Game& game,
	bool running) const
{
	GameMcpCommands::PrunePendingBallAdjustments(game);
	const GameMcpReadSnapshot snapshot = GameMcpReadView::Capture(game);
	const auto isBallUnlocked = [&snapshot](const std::string& definitionId)
	{
		return std::find(
			snapshot.metaProgression.unlockedBallIds.begin(),
			snapshot.metaProgression.unlockedBallIds.end(),
			definitionId) != snapshot.metaProgression.unlockedBallIds.end();
	};
	nlohmann::json state = {
		{ "schema_version", 1 },
		{ "running", running },
		{ "published_at_unix_ms", UnixTimeMilliseconds() },
		{ "sequence", m_StateSequence },
		{ "scene", GetSceneName(game.GetCurrentScene()) },
		{ "game_state", GetGameStateName(game) },
		{ "battle_state", ToString(game.GetBattleState()) },
		{ "battle_result", ToString(game.GetLastBattleResult()) },
		{ "clear_reward_active", game.IsClearRewardActive() },
		{ "selected_stage_id", game.GetSelectedStageId() },
		{ "autoplay_enabled", snapshot.autoplayEnabled },
		{ "autoplay_stop_after_current_run",
			snapshot.autoplayStopAfterCurrentRun },
		{ "autoplay_stop_after_current_run_default",
			snapshot.autoplayStopAfterCurrentRunDefault },
		{ "bridge", {
			{ "write_actions_enabled", m_AllowWriteActions },
			{ "single_pending_command", true },
		} },
	};

	state["physics_clock"] = {
		{ "model", BallPhysicsWorld::ModelName },
        { "trajectory_prediction", "shared_ccd_toi_v1" },
		{ "substeps_last_tick", snapshot.physics.substepsLastTick },
        { "max_toi_iterations_per_tick", ContinuousBallStepper::MaxIterations },
		{ "substep_limit_count", snapshot.physics.substepLimitCount },
		{ "frequency_hz", 60 },
		{ "tick", snapshot.physics.tick },
		{ "steps_last_frame", snapshot.physics.stepsLastFrame },
		{ "max_steps_per_frame", FixedStepClock::MaxStepsPerFrame },
		{ "dropped_seconds", snapshot.physics.droppedSeconds },
	};
	state["table"] = {
		{ "field_width", TableConfig::GetFieldWidth() },
		{ "field_depth", TableConfig::GetFieldDepth() },
		{ "walls", nlohmann::json::array() },
		{ "pockets", nlohmann::json::array() },
	};
	state["rest_heal"] = {
		{ "heal_ratio", snapshot.restHealRatio },
		{ "heal_percent", game.GetRestHealPercent() },
		{ "configured_heal_amount", game.GetRestHealAmount() },
		{ "capped_at_max_hp", true },
		{ "available", game.CanRestHeal() },
	};
	state["meta_progression"] = {
		{"active_ascension", snapshot.metaProgression.activeAscension},
		{"selected_ascension", snapshot.metaProgression.selectedAscension},
		{"highest_unlocked_ascension", snapshot.metaProgression.highestUnlockedAscension},
		{"total_runs", snapshot.metaProgression.totalRuns},
		{"total_clears", snapshot.metaProgression.totalClears},
		{"persistent_rewards_eligible", snapshot.metaProgression.persistentRewardsEligible},
		{"enemy_hp_multiplier", ProgressionProfile::EnemyHpMultiplier(snapshot.metaProgression.activeAscension)},
		{"enemy_attack_bonus", ProgressionProfile::EnemyAttackBonus(snapshot.metaProgression.activeAscension)},
	};
	state["meta_progression"]["ascension_rules"] = nlohmann::json::array();
	for (int level = 0; level <= ProgressionProfile::MaximumAscension; ++level)
	{
		state["meta_progression"]["ascension_rules"].push_back({
			{ "level", level },
			{ "rule", ProgressionProfile::AscensionRule(level) },
			{ "unlocked", level <= snapshot.metaProgression.highestUnlockedAscension },
		});
	}
	state["meta_progression"]["achievements"] = nlohmann::json::array();
	for (std::size_t achievementIndex = 0;
		achievementIndex < AchievementCatalog.size(); ++achievementIndex)
	{
		const AchievementDefinition& achievement =
			AchievementCatalog[achievementIndex];
		state["meta_progression"]["achievements"].push_back({
			{ "key", achievement.key },
			{ "name", achievement.name },
			{ "condition", achievement.condition },
			{ "reward", achievement.reward },
			{ "unlocked", snapshot.metaProgression.achievementUnlocked[achievementIndex] },
		});
	}
	state["meta_progression"]["ball_unlocks"] = {
		{{ "definition_id", "player_standard" }, { "unlocked", true }},
		{{ "definition_id", "player_heavy" }, { "unlocked", true }},
		{{ "definition_id", "player_chain_impact" }, { "unlocked", true }},
		{{ "definition_id", "player_pierce" }, { "unlocked", isBallUnlocked("player_pierce") }},
		{{ "definition_id", "player_refractive_pierce" }, { "unlocked", isBallUnlocked("player_refractive_pierce") }},
		{{ "definition_id", "player_trace_driver" }, { "unlocked", isBallUnlocked("player_trace_driver") }},
		{{ "definition_id", "player_pierce_finisher" }, { "unlocked", isBallUnlocked("player_pierce_finisher") }},
		{{ "definition_id", "player_bounce" }, { "unlocked", isBallUnlocked("player_bounce") }},
		{{ "definition_id", "player_cushion_charge" }, { "unlocked", isBallUnlocked("player_cushion_charge") }},
		{{ "definition_id", "player_ricochet_finisher" }, { "unlocked", isBallUnlocked("player_ricochet_finisher") }},
		{{ "definition_id", "player_anchor" }, { "unlocked", isBallUnlocked("player_anchor") }},
		{{ "definition_id", "player_stop_shield" }, { "unlocked", isBallUnlocked("player_stop_shield") }},
		{{ "definition_id", "player_anchor_finisher" }, { "unlocked", isBallUnlocked("player_anchor_finisher") }},
	};
	const std::vector<TableFrame*> tableFrames =
		GameRuntime::World().GetComponents<TableFrame>();
	for (const TableFrame* tableFrame : tableFrames)
	{
		if (tableFrame == nullptr)
		{
			continue;
		}
		for (const Collision::Segment& wall :
			tableFrame->GetWalls())
		{
			state["table"]["walls"].push_back({
				{ "start", VectorToJson(wall.start) },
				{ "end", VectorToJson(wall.end) },
			});
		}
	}
	const std::vector<Pocket*> pockets =
		GameRuntime::World().GetComponents<Pocket>();
	for (std::size_t pocketIndex = 0;
		pocketIndex < pockets.size();
		pocketIndex++)
	{
		const Pocket* pocket = pockets[pocketIndex];
		if (pocket == nullptr)
		{
			continue;
		}
		const Collision::Sphere sphere = pocket->GetSphere();
		state["table"]["pockets"].push_back({
			{ "index", pocketIndex },
			{ "position", VectorToJson(sphere.center) },
			{ "radius", sphere.radius },
		});
	}

	state["pocket_rules"] = {
		{ "player", {
			{ "max_hp_damage_ratio", snapshot.pocketRules.playerDamageRatio },
			{ "damage_amount", game.GetPlayerPocketDamageAmount() },
			{ "damage_ignores_defense", true },
			{ "return_region", {
				{ "center", VectorToJson(
					DirectX::SimpleMath::Vector3(
						0.0f, TableConfig::FIELD_HEIGHT, 0.0f)) },
				{ "half_width", snapshot.pocketRules.playerReturnHalfWidth },
				{ "half_depth", snapshot.pocketRules.playerReturnHalfDepth },
			} },
		} },
		{ "enemy", {
			{ "high_hp_result", "skip_attack_then_queue_return" },
			{ "returns_per_turn", 1 },
			{ "return_position", VectorToJson(
				DirectX::SimpleMath::Vector3(
					snapshot.pocketRules.enemyReturnX,
					TableConfig::FIELD_HEIGHT,
					TableConfig::GetFieldDepth() * 0.5f -
						snapshot.pocketRules.enemyReturnTopEdgeOffset)) },
			{ "finisher_hp_ratios", {
				{ "normal", snapshot.pocketRules.normalFinisherRatio },
				{ "midboss", snapshot.pocketRules.midBossFinisherRatio },
				{ "boss", snapshot.pocketRules.bossFinisherRatio },
			} },
			{ "current_stage_type",
				StageTypeToMcpString(snapshot.pocketRules.stageType) },
			{ "current_finisher_hp_ratio",
				game.GetCurrentPocketFinisherRatio() },
			{ "return_queue_size", snapshot.pocketRules.returnQueueSize },
		} },
	};

	state["stage_layout_control"] = {
		{ "application_timing", "next_battle_spawn" },
		{ "maximum_enemy_count", kMaximumMcpStageEnemyCount },
		{ "allowed_enemy_ids", m_StageEnemyIds },
		{ "queued_override",
			snapshot.queuedStageOverride.has_value()
				? StageDataToJson(snapshot.queuedStageOverride.value())
				: nlohmann::json(nullptr) },
		{ "current_override",
			snapshot.currentStageOverride.has_value()
				? StageDataToJson(snapshot.currentStageOverride.value())
				: nlohmann::json(nullptr) },
	};

	const std::vector<PlayerBall*> players =
		GameRuntime::World().GetComponents<PlayerBall>();
	const PlayerBall* player =
		players.empty() ? nullptr : players[0];
	state["player"] = {
		{ "current_hp",
			player == nullptr
				? game.GetPlayerCurrentHp()
				: player->GetHP() },
		{ "max_hp",
			player == nullptr
				? game.GetPlayerMaxHp()
				: player->GetMaxHP() },
		{ "money", game.GetPlayerMoney() },
		{ "progress", snapshot.progressionScaling.currentProgress },
		{ "pocketed", player != nullptr && player->IsPocketed() },
	};
	state["player"]["temporary_shield"] = game.GetPlayerShield();
	const RunResultSnapshot& runStatistics = snapshot.runStatistics;
	state["run_progress"] = {
		{ "phase", ToString(game.GetRunPhase()) },
		{ "area_progress", game.GetAreaProgress() },
		{ "area_goal", game.GetNormalRouteAreaGoal() },
		{ "total_battles", runStatistics.totalBattles },
		{ "midboss_challenges", runStatistics.midBossChallenges },
		{ "midboss_defeats", runStatistics.midBossDefeats },
		{ "final_boss_reached", runStatistics.finalBossReached },
		{ "final_boss_defeated", runStatistics.finalBossDefeated },
		{ "final_boss_id", runStatistics.finalBossId },
	};
	if (player != nullptr)
	{
		state["player"]["position"] =
			VectorToJson(player->GetPosition());
		state["player"]["attack"] = player->GetAttack();
		state["player"]["velocity"] = VectorToJson(player->GetVelocity());
		state["player"]["idle"] = player->IsIdle();
		if (player->GetBall() != nullptr)
		{
			state["player"]["radius"] =
				player->GetBall()->GetRadius();
		}
		const PlayerBallData* selectedBall =
			game.GetBallOffer(snapshot.selectedOfferIndex);
		if (selectedBall == nullptr)
		{
			selectedBall = game.GetCurrentPlayerBallData();
		}
		if (selectedBall != nullptr)
		{
			state["player"]["ball"] = BallDataToJson(
				*selectedBall, snapshot.selectedOfferIndex);
		}
	}

	state["relics"] = nlohmann::json::array();
	for (int index = 0; index < game.GetRelicCount(); index++)
	{
		const RelicDefinition* relic = game.GetRelic(index);
		if (relic == nullptr)
		{
			continue;
		}

		state["relics"].push_back({
			{ "index", index },
			{ "name", relic->name },
			{ "description", relic->description },
			{ "price", relic->price },
			{ "rarity", ToString(relic->rarity) },
			{ "midboss_weight", relic->midBossWeight },
			{ "shop_weight", relic->shopWeight },
			{ "owned", game.HasRelic(relic->type) },
			{ "unlocked", snapshot.metaProgression.relicUnlocked[static_cast<std::size_t>(index)] },
			{ "shop_offered", game.IsShopRelicOffered(index) },
			{ "midboss_offered", [&game, index]()
				{
					for (int offerIndex = 0;
						offerIndex < game.GetMidBossRelicOfferCount();
						offerIndex++)
					{
						if (game.GetMidBossRelicOfferCatalogIndex(offerIndex) == index)
						{
							return true;
						}
					}
					return false;
				}() },
		});
	}
	state["relic_selection"] = {
		{ "shop_offer_count", game.GetShopRelicOfferCount() },
		{ "shop_purchase_limited", false },
		{ "shop_purchase_used", false },
		{ "midboss_active", game.IsMidBossRelicSelectionActive() },
		{ "midboss_offer_count", game.GetMidBossRelicOfferCount() },
	};
	state["relic_effects"] = {
		{ "all_ball_attack_bonus", game.GetRelicAttackBonus() },
		{
			"current_shot_collision_attack_bonus",
			game.GetCurrentShotCollisionAttackBonus()
		},
		{
			"current_shot_enemy_damage_bonus",
			game.GetCurrentShotCollisionAttackBonus()
		},
		{
			"current_shot_player_enemy_collisions",
			game.GetCurrentShotPlayerEnemyCollisionCount()
		},
		{
			"current_shot_enemy_enemy_collisions",
			game.GetCurrentShotEnemyEnemyCollisionCount()
		},
		{
			"current_shot_ball_collisions",
			game.GetCurrentShotBallCollisionCount()
		},
		{
			"bank_shot_ready",
			game.IsCurrentShotBankShotReady()
		},
		{
			"bank_shot_damage_multiplier",
			game.HasRelic(RelicType::BankShot) ? 2 : 1
		},
		{
			"emergency_repair_contact_threshold",
			game.HasRelic(RelicType::EmergencyRepairKit) ? 3 : 0
		},
		{
			"emergency_repair_heal_amount",
			game.HasRelic(RelicType::EmergencyRepairKit) ? 1 : 0
		},
		{
			"applies_to_collision_pairs",
			nlohmann::json::array(
				{ "player_enemy", "enemy_enemy" })
		},
	};

    state["boss_state"] = nullptr;
    state["break_balls"] = nlohmann::json::array();
    for (auto* neutral : GameRuntime::World().GetComponents<BreakBall>())
    {
        auto* ball = neutral->GetBall();
        state["break_balls"].push_back({
            {"target_id", "break_ball:" + std::to_string(neutral->GetIndex())},
            {"position", VectorToJson(ball->GetPosition())}, {"velocity", VectorToJson(ball->GetVelocity())},
            {"radius", ball->GetRadius()}, {"mass", ball->GetStatus().mass},
            {"active", neutral->GetGameObject()->IsActive()}, {"used_this_shot", neutral->IsUsed()},
            {"pocketed", neutral->IsPocketed()}, {"fixed_boss_damage", BossCombatRules::BreakBallDamage},
            {"armor_damage", 1}, {"max_activations_per_shot", 1}, {"pierce_passes_through", false},
            {"consumes_enemy_damage_relics", false}});
    }
	state["enemies"] = nlohmann::json::array();
	const std::vector<EnemyBall*> enemies =
		GameRuntime::World().GetComponents<EnemyBall>();
	for (std::size_t enemyIndex = 0;
		enemyIndex < enemies.size();
		enemyIndex++)
	{
		EnemyBall* enemy = enemies[enemyIndex];
		if (enemy == nullptr)
		{
			continue;
		}
        if (enemy->IsArmorBoss())
        {
            const auto& boss = enemy->GetBossState();
            state["boss_state"] = {
                {"boss_id", enemy->GetEnemyId()}, {"target_id", "enemy:" + std::to_string(enemyIndex)},
                {"phase", 1}, {"hp", enemy->GetHP()}, {"max_hp", enemy->GetMaxHP()},
                {"armor", boss.armor}, {"max_armor", BossCombatRules::MaxArmor},
                {"is_broken", boss.IsBroken()}, {"break_shots_remaining", boss.shotsRemaining},
                {"break_started_this_shot", boss.startedThisShot}, {"pocket_immune", true},
                {"direct_damage_multiplier", boss.IsBroken() ? 1.0f : 0.25f},
                {"damage_order", "directional_then_defense_then_armor_ceil_min1"},
                {"break_ball_refreshes_break", false}, {"trigger_shot_consumes_break", false}};
        }
		state["enemies"].push_back({
			{ "target_id",
				"enemy:" + std::to_string(enemyIndex) },
			{ "enemy_id", enemy->GetEnemyId() },
			{ "hp", enemy->GetHP() },
			{ "max_hp", enemy->GetMaxHP() },
			{ "attack", enemy->GetAttack() },
			{ "defense", enemy->GetDefense() },
			{ "mass", enemy->GetStatus().mass },
			{ "friction", enemy->GetStatus().friction },
			{ "restitution", enemy->GetStatus().restitution },
			{ "frontal_damage_multiplier", enemy->GetFrontalDamageMultiplier() },
			{ "pocket_damage_ratio", enemy->GetPocketDamageRatio() },
			{ "defeated", enemy->IsDefeated() },
			{ "pocketed", enemy->IsPocketed() },
            { "pocket_immune", enemy->IsArmorBoss() },
			{ "can_attack_this_turn",
				!enemy->IsDefeated() && !enemy->IsPocketed() },
			{ "pocket_queue_index",
				game.GetPocketQueueIndex(enemy) },
			{ "hp_ratio",
				enemy->GetMaxHP() <= 0
					? 0.0f
					: static_cast<float>(enemy->GetHP()) /
						static_cast<float>(enemy->GetMaxHP()) },
			{ "pocket_finisher_eligible",
				game.IsEnemyPocketFinisherEligible(enemy) },
			{ "stopped", enemy->IsStopped() },
			{ "radius", enemy->GetRadius() },
			{ "position", VectorToJson(enemy->GetPosition()) },
			{ "velocity", VectorToJson(enemy->GetVelocity()) },
		});
	}

	state["offered_balls"] = nlohmann::json::array();
	for (int index = 0;
		index < game.GetBallOfferCount();
		index++)
	{
		const PlayerBallData* ball =
			game.GetBallOffer(index);
		if (ball == nullptr)
		{
			continue;
		}
		nlohmann::json ballJson =
			BallDataToJson(*ball, index);
		ballJson["selected"] =
			index == snapshot.selectedOfferIndex;
		ballJson["held"] =
			index == snapshot.selectedHoldIndex;
		state["offered_balls"].push_back(
			std::move(ballJson));
	}

	state["deck_balls"] = nlohmann::json::array();
	for (int index = 0;
		index < game.GetDeckBallCount();
		index++)
	{
		const PlayerBallData* ball =
			game.GetDeckBall(index);
		if (ball != nullptr)
		{
			nlohmann::json ballJson =
				BallDataToJson(*ball, index);
			const int upgradeCost =
				game.GetClearRewardUpgradeCost(index);
			ballJson["clear_reward_upgrade_cost"] =
				upgradeCost >= 0
				? nlohmann::json(upgradeCost)
				: nlohmann::json(nullptr);
			ballJson["clear_reward_upgrade_affordable"] =
				upgradeCost >= 0 &&
				game.GetPlayerMoney() >= upgradeCost;
			state["deck_balls"].push_back(
				std::move(ballJson));
		}
	}

	state["catalog_balls"] = nlohmann::json::array();
	for (int index = 0;
		index < game.GetShopBallCount();
		index++)
	{
		const PlayerBallData* ball =
			game.GetShopBall(index);
		if (ball != nullptr)
		{
			state["catalog_balls"].push_back(
				BallDataToJson(*ball, index));
		}
	}
	state["shop_ball_offers"] = nlohmann::json::array();
	for (int offerIndex = 0;
		offerIndex < game.GetShopBallOfferCount();
		offerIndex++)
	{
		const int catalogIndex =
			game.GetShopBallOfferCatalogIndex(offerIndex);
		const PlayerBallData* ball =
			game.GetShopBallOffer(offerIndex);
		if (ball == nullptr)
		{
			continue;
		}
		nlohmann::json offer = BallDataToJson(*ball, offerIndex);
		offer["offer_index"] = offerIndex;
		offer["catalog_index"] = catalogIndex;
		state["shop_ball_offers"].push_back(std::move(offer));
	}
	state["clear_reward_ball_offers"] = nlohmann::json::array();
	for (int offerIndex = 0;
		offerIndex < game.GetClearRewardBallOfferCount();
		offerIndex++)
	{
		const int catalogIndex =
			game.GetClearRewardBallOfferCatalogIndex(offerIndex);
		const PlayerBallData* ball =
			game.GetClearRewardBallOffer(offerIndex);
		if (ball == nullptr)
		{
			continue;
		}
		nlohmann::json offer = BallDataToJson(*ball, offerIndex);
		offer["offer_index"] = offerIndex;
		offer["catalog_index"] = catalogIndex;
		state["clear_reward_ball_offers"].push_back(std::move(offer));
	}

	const int deckBallCount =
		game.GetDeckBallCount();
	state["deck_rule"] = {
		{ "minimum_size", game.GetMinimumDeckSize() },
		{ "current_size", deckBallCount },
		{ "shop_removal_cost", kShopRemoveCost },
		{ "can_remove",
			deckBallCount > game.GetMinimumDeckSize() &&
			game.GetPlayerMoney() >= kShopRemoveCost },
	};

	state["ball_adjustment_rule"] = {
		{
			"trigger",
			"the selected player ball hit a full-HP enemy and the enemy survived"
		},
		{ "purpose", "automatic-play suggestion only" },
		{ "required_for_upgrade", false },
		{ "required_for_reward_upgrade", false },
		{ "required_for_removal", false },
		{ "consumed_after_adjustment", true },
	};
	state["ball_adjustment_candidates"] =
		nlohmann::json::array();
	for (const std::uint64_t instanceId : snapshot.pendingBallAdjustments)
	{
		const int ballIndex =
			FindDeckBallIndex(game, instanceId);
		const PlayerBallData* ball =
			ballIndex < 0
				? nullptr
				: game.GetDeckBall(ballIndex);
		if (ball == nullptr)
		{
			continue;
		}

		nlohmann::json candidate =
			BallDataToJson(*ball, ballIndex);
		candidate["allowed_adjustments"] =
			ball->CanUpgrade()
				? nlohmann::json::array(
					{ "upgrade", "remove" })
				: nlohmann::json::array(
					{ "remove" });
		state["ball_adjustment_candidates"].push_back(
			std::move(candidate));
	}

	state["dynamic_balance"] = {
		{ "role", "retired_compatibility" },
		{ "retired", true },
		{ "enabled", false },
		{ "current_battle_enabled", false },
		{ "level", 0 },
		{ "current_battle_level", 0 },
		{ "minimum_level", 0 },
		{ "maximum_level", 0 },
		{ "application_timing", "next_battle_spawn" },
		{ "next_enemy_modifier", {
			{ "max_hp_delta", 0 },
			{ "attack_delta", 0 },
		} },
		{ "current_stage_metrics", {
			{ "active", false },
			{ "shots", 0 },
			{ "no_hit_shots", 0 },
			{ "enemy_count", 0 },
		} },
		{ "last_evaluation", {
			{ "result", "not_evaluated" },
			{ "reason", "Dynamic difficulty was retired; fixed difficulty is active." },
			{ "level_change", 0 },
			{ "remaining_hp_ratio", 1.0f },
			{ "no_hit_rate", 0.0f },
			{ "shots_per_enemy", 0.0f },
		} },
	};
	state["progression_scaling"] = {
		{ "enabled", snapshot.progressionScaling.enabled },
		{ "current_progress", snapshot.progressionScaling.currentProgress },
		{ "next_enemy_hp_delta",
			snapshot.progressionScaling.nextEnemyHpDelta },
		{ "hp_start_progress", snapshot.progressionScaling.hpStartProgress },
		{ "hp_interval", snapshot.progressionScaling.hpInterval },
		{ "hp_step", snapshot.progressionScaling.hpStep },
		{ "maximum_hp_delta",
			snapshot.progressionScaling.maximumHpDelta },
		{ "next_enemy_attack_delta",
			snapshot.progressionScaling.nextEnemyAttackDelta },
		{ "attack_start_progress", snapshot.progressionScaling.attackStartProgress },
		{ "attack_interval", snapshot.progressionScaling.attackInterval },
		{ "attack_step", snapshot.progressionScaling.attackStep },
		{ "maximum_attack_delta",
			snapshot.progressionScaling.maximumAttackDelta },
	};
	state["baseline_difficulty"] = {
		{ "profile", snapshot.baselineDifficultyProfile },
		{ "enemy_hp_multiplier", snapshot.baselineEnemyHpMultiplier },
		{ "enemy_attack_delta", snapshot.baselineEnemyAttackDelta },
	};
	state["balance_validation"] = {
		{ "enabled", snapshot.balanceValidation.enabled },
		{ "experiment_id", snapshot.balanceValidation.experimentId },
		{ "variant_id", snapshot.balanceValidation.variantId },
		{ "variant_index", snapshot.balanceValidation.variantIndex },
		{ "variant_count", snapshot.balanceValidation.variantCount },
		{ "run_seed", snapshot.balanceValidation.runSeed },
		{ "stage_selection_seed", snapshot.balanceValidation.stageSelectionSeed },
		{ "route_selection_seed", snapshot.balanceValidation.routeSelectionSeed },
		{ "seed_suite_index", snapshot.balanceValidation.seedSuiteIndex },
		{ "seed_suite_size", snapshot.balanceValidation.seedSuiteSize },
		{ "maximum_cleared_stages",
			snapshot.balanceValidation.maximumClearedStages },
		{ "endurance_mode", snapshot.balanceValidation.enduranceMode },
		{ "cleared_stage_count", snapshot.balanceValidation.clearedStageCount },
		{
			"dynamic_balance_forced_off",
			true
		},
	};

	state["available_actions"] = nlohmann::json::array();
	state["route_options"] = nlohmann::json::array();
	state["run_map"] = game.GetRunMap().Snapshot();
	if (dynamic_cast<StageSelectScene*>(game.GetCurrentScene()) == nullptr)
		for (auto& node : state["run_map"]["nodes"]) node["selectable"] = false;
	state["available_actions"].push_back("set_next_stage_layout");
	if (snapshot.queuedStageOverride.has_value())
	{
		state["available_actions"].push_back(
			"clear_next_stage_layout");
	}
	const std::string scene = state["scene"].get<std::string>();
	if (scene == "title" || scene == "result")
	{
		state["available_actions"].push_back(
			"start_new_run");
	}
	else if (scene == "stage_select")
	{
		StageSelectScene* stageSelect =
			dynamic_cast<StageSelectScene*>(game.GetCurrentScene());
		if (stageSelect != nullptr)
		{
			for (int routeIndex = 0;
				routeIndex < stageSelect->GetRouteNodeCount();
				routeIndex++)
			{
				state["route_options"].push_back({
					{ "route_index", routeIndex },
					{ "node_id", stageSelect->GetMapNodeIdAt(routeIndex) },
					{ "next_node_ids", game.GetRunMap().Node(stageSelect->GetMapNodeIdAt(routeIndex))->next },
					{ "destination",
						stageSelect->GetRouteIdAt(routeIndex) },
					{ "display_name",
						stageSelect->GetRouteDisplayNameAt(routeIndex) },
				});
			}
		}
		state["available_actions"].push_back(
			"choose_destination");
	}
	else if (scene == "rest_site")
	{
		RestSiteScene* restSite =
			dynamic_cast<RestSiteScene*>(game.GetCurrentScene());
		const bool actionUsed =
			restSite != nullptr && restSite->HasUsedAction();
		const bool canHeal = game.CanRestHeal();
		bool canUpgrade = false;
		for (int index = 0;
			index < game.GetDeckBallCount();
			index++)
		{
			const PlayerBallData* ball =
				game.GetDeckBall(index);
			if (ball != nullptr && ball->CanUpgrade())
			{
				canUpgrade = true;
				break;
			}
		}
		const bool bonusAvailable = canHeal || canUpgrade;

		state["rest_site_rule"] = {
			{ "must_claim_available_bonus_before_continue", true },
			{ "action_used", actionUsed },
			{ "bonus_available", bonusAvailable },
			{ "heal_ratio", snapshot.restHealRatio },
			{ "heal_percent", game.GetRestHealPercent() },
			{ "configured_heal_amount", game.GetRestHealAmount() },
			{ "priority", nlohmann::json::array({
				"heal_if_hp_below_40_percent",
				"upgrade_any_available_deck_ball",
				"heal_as_fallback_if_hp_is_not_full"
			}) },
		};

		if (!actionUsed && canHeal)
		{
			state["available_actions"].push_back("heal");
		}
		if (!actionUsed && canUpgrade)
		{
			state["available_actions"].push_back(
				"upgrade_ball");
		}
		if (actionUsed || !bonusAvailable)
		{
			state["available_actions"].push_back(
				"continue_to_battle");
		}
	}
	else if (scene == "shop")
	{
		for (int offerIndex = 0;
			offerIndex < game.GetShopRelicOfferCount(); offerIndex++)
		{
			const RelicDefinition* relic = game.GetShopRelicOffer(offerIndex);
			if (relic != nullptr &&
				!game.HasRelic(relic->type) &&
				game.GetPlayerMoney() >= relic->price)
			{
				state["available_actions"].push_back(
					"buy_relic");
				break;
			}
		}
		if (game.GetPlayerMoney() >= kShopRemoveCost &&
			game.GetDeckBallCount() >
				game.GetMinimumDeckSize())
		{
			state["available_actions"].push_back(
				"remove_ball");
		}
		state["available_actions"].push_back(
			"continue_to_battle");
	}
	else if (scene == "battle" &&
		game.GetBattleState() == BattleState::AimingDirection &&
		player != nullptr &&
		player->IsIdle() &&
		game.AreAllBallsStopped())
	{
		state["available_actions"].push_back(
			"select_ball");
		state["available_actions"].push_back(
			"fire_shot");
        if (!state["boss_state"].is_null())
        {
            state["available_actions"].push_back("evaluate_boss_shots");
            state["available_actions"].push_back("fire_boss_shot");
        }
	}

	if (game.IsClearRewardActive())
	{
		state["clear_reward_rule"] = {
			{ "new_ball_offer_count", game.GetClearRewardBallOfferCount() },
			{ "new_ball_duplicates_allowed", true },
			{ "new_ball_selection_source", "clear_reward_ball_offers" },
			{ "upgrade_requires_money", true },
			{ "upgrade_cost_by_current_level", {
				{ "0", 15 },
				{ "1", 30 },
			} },
			{ "rest_site_upgrade_requires_money", false },
		};
		if (game.IsMidBossRelicSelectionActive())
		{
			state["available_actions"].push_back("choose_relic");
		}
		else if (game.IsClearRewardChosen())
		{
			state["available_actions"].push_back(
				"continue_after_reward");
		}
		else
		{
			state["available_actions"].push_back(
				"choose_reward");
		}
	}

	state["debug_mode"] = {
		{"active", snapshot.debug.active},
		{"editor_open", snapshot.debug.editorOpen},
		{"finished", snapshot.debug.finished},
	};
	if (!snapshot.debug.stageEditor.is_null())
	{
		state["stage_editor"] = snapshot.debug.stageEditor;
	}
	state["table"]["cushions"] = nlohmann::json::array();
	state["table"]["cushion_boost_consumed_this_shot"] =
		game.WasCushionBoostConsumedThisShot();
	state["ball_synergies"] = {
		{ "heavy_stacks", game.GetHeavyCollisionCount() },
		{ "pierce_stacks", game.GetPierceTraceState().stackCount },
		{ "player_anchor_stacks", game.GetPlayerAnchorStacks() },
		{ "cushion_strong_uses_this_shot", game.GetCushionStrongUsesThisShot() },
		{ "pierce_traces", nlohmann::json::array() },
		{ "enemy_anchor_stacks", nlohmann::json::array() },
	};
	for (const auto& trace : game.GetPierceTraceState().traces)
	{
		state["ball_synergies"]["pierce_traces"].push_back({
			{ "id", trace.id },
			{ "start", { trace.start.x, trace.start.y, trace.start.z } },
			{ "end", { trace.end.x, trace.end.y, trace.end.z } },
			{ "direction", { trace.direction.x, trace.direction.y, trace.direction.z } },
		});
	}
	for (EnemyBall* enemy : GameRuntime::World().GetComponents<EnemyBall>())
	{
		if (enemy == nullptr) continue;
		state["ball_synergies"]["enemy_anchor_stacks"].push_back({
			{ "enemy_id", enemy->GetEnemyId() },
			{ "stacks", enemy->GetAnchorStacks() },
		});
	}
	const auto& cushionCharges = game.GetCushionCharges();
	state["table"]["reflection_stacks"] = cushionCharges.stackCount;
	state["table"]["reflection_setup_pending"] =
		cushionCharges.pendingSetupBonus;
	if (snapshot.debug.editorOpen)
	{
		state["available_actions"] = {
			"validate_stage_layout", "propose_stage_layout" };
	}
	else if (snapshot.debug.active)
	{
		auto allowed = nlohmann::json::array();
		for (const auto& action : state["available_actions"])
			if (action == "select_ball" || action == "fire_shot" || action == "evaluate_boss_shots" || action == "fire_boss_shot")
				allowed.push_back(action);
		state["available_actions"] = std::move(allowed);
	}
	if (dynamic_cast<TitleScene*>(game.GetCurrentScene()) != nullptr &&
		!snapshot.debug.editorOpen)
		state["available_actions"].push_back("open_stage_editor");
	return state;
}

void GameMcpBridge::PublishState(
	Game& game,
	bool running)
{
	m_StateSequence++;
	if (!WriteJsonAtomically(
		GetStatePath(),
		BuildState(game, running)))
	{
		std::cerr
			<< "[GameMcpBridge] Failed to publish game state"
			<< std::endl;
	}
}

nlohmann::json GameMcpBridge::ExecuteCommand(
	Game& game,
	const nlohmann::json& command)
{
	if (!m_AllowWriteActions)
	{
		return CommandResult(
			false,
			"Write actions are disabled by game_mcp_bridge.json.");
	}

	const std::string action =
		command.value("action", std::string());
	if (action == "open_stage_editor")
	{
		if (dynamic_cast<TitleScene*>(game.GetCurrentScene()) == nullptr) return CommandResult(false, "Open the stage editor from the title screen.");
		game.OpenDebugMode();
		return CommandResult(true, "Stage editor opened in debug setup.");
	}
	if (action == "validate_stage_layout" || action == "propose_stage_layout")
	{
		if (!game.IsDebugEditorOpen()) return CommandResult(false, "Open the stage editor first.");
		const auto args = command.value("arguments", nlohmann::json::object());
		const auto layout = args.value("layout", nlohmann::json());
		const auto report = GameMcpCommands::InspectStageLayout(game, layout);
		auto result = CommandResult(report["valid"].get<bool>(), "Layout validation complete.");
		result["report"] = report;
		if (action == "propose_stage_layout" && report["valid"].get<bool>())
		{
			try
			{
				const auto& expected = args.at("expected_revision");
				if (!expected.is_number_integer() || expected.get<double>() < 0) throw std::runtime_error("expected_revision must be a nonnegative integer.");
				GameMcpCommands::ProposeStageLayout(
					game,
					layout,
					expected.get<std::uint64_t>());
				result["message"] = "Proposal ready for visual review. Draft and files are unchanged until accepted in the editor.";
			}
			catch (const std::exception& e) { result = CommandResult(false, e.what()); result["report"] = report; }
		}
		return result;
	}
	if (game.IsDebugEditorOpen())
		return CommandResult(false, "Debug setup is open. Resume or start the battle from its window.");
	if (game.IsDebugMode() && action != "select_ball" && action != "fire_shot" &&
		action != "evaluate_boss_shots" && action != "fire_boss_shot")
		return CommandResult(false, "This action is unavailable in debug battle mode.");
	const nlohmann::json arguments =
		command.value(
			"arguments",
			nlohmann::json::object());
	const std::string scene = GetSceneName(game.GetCurrentScene());
	auto recordBuildDecision = [&game, &arguments]()
	{
		if (arguments.contains("decision_context") &&
			arguments["decision_context"].is_object())
		{
			game.RecordBalanceEvent(
				"mcp_build_decision",
				arguments["decision_context"]);
		}
	};

    if (action == "evaluate_boss_shots" || action == "fire_boss_shot")
    {
		if (scene != "battle" ||
			game.GetBattleState() != BattleState::AimingDirection ||
			!game.AreAllBallsStopped())
            return CommandResult(false, "Boss planning requires a stopped battle in aiming state.");
        const auto choices = game.EvaluateBossShots();
        if (choices.empty()) return CommandResult(false, "No live Armor boss or playable ball.");
        if (action == "evaluate_boss_shots")
        {
            auto result = CommandResult(true, "Evaluated with shared CCD/TOI prediction; gameplay unchanged.");
            result["evaluation"] = choices;
            return result;
        }
        const bool fired = game.FireBossPlannedShot(arguments.value("candidate_id", std::string()),
            arguments.value("state_key", std::string()));
        return CommandResult(fired, fired ? "Fired the selected boss plan." : "Stale or invalid plan. Evaluate again.");
    }
	if (action == "set_next_stage_layout")
	{
		if (!arguments.contains("layout_id") ||
			!arguments["layout_id"].is_string())
		{
			return CommandResult(
				false,
				"layout_id must be a string.");
		}
		const std::string layoutId =
			arguments["layout_id"].get<std::string>();
		if (!IsValidLayoutId(layoutId))
		{
			return CommandResult(
				false,
				"layout_id must be 1-64 characters using letters, numbers, _, -, or ..");
		}

		const std::string stageTypeName =
			arguments.value(
				"stage_type",
				std::string("normal"));
		if (stageTypeName != "normal" &&
			stageTypeName != "midboss" &&
			stageTypeName != "boss")
		{
			return CommandResult(
				false,
				"stage_type must be normal, midboss, or boss.");
		}

		if (!arguments.contains("difficulty") ||
			!arguments["difficulty"].is_number_integer() ||
			!arguments.contains("par") ||
			!arguments["par"].is_number_integer())
		{
			return CommandResult(
				false,
				"difficulty and par must be integers.");
		}
		const int difficulty =
			arguments["difficulty"].get<int>();
		const int par = arguments["par"].get<int>();
		if (difficulty < 1 || difficulty > 99 ||
			par < 1 || par > 99)
		{
			return CommandResult(
				false,
				"difficulty and par must be between 1 and 99.");
		}

		if (!arguments.contains("enemies") ||
			!arguments["enemies"].is_array())
		{
			return CommandResult(
				false,
				"enemies must be an array.");
		}
		const nlohmann::json& enemyArray =
			arguments["enemies"];
		if (enemyArray.empty() ||
			enemyArray.size() > kMaximumMcpStageEnemyCount)
		{
			return CommandResult(
				false,
				"enemies must contain between 1 and 12 placements.");
		}

		std::unordered_map<std::string, EnemyData>
			enemyDefinitions;
		for (const EnemyData& enemy :
			StageDataLoader::LoadEnemyDefinitions(
				"assets/data/enemy_data.json"))
		{
			enemyDefinitions.emplace(enemy.id, enemy);
		}
		if (enemyDefinitions.empty())
		{
			return CommandResult(
				false,
				"Enemy definitions could not be loaded.");
		}

		StageData stage;
		stage.id = layoutId;
		stage.stageType = ParseStageType(stageTypeName);
		stage.difficulty = difficulty;
		stage.par = par;
		stage.enemies.reserve(enemyArray.size());

		const float fieldHalfWidth =
			TableConfig::GetFieldWidth() * 0.5f;
		const float fieldHalfDepth =
			TableConfig::GetFieldDepth() * 0.5f;
		const float playerRadius = (std::max)(
			0.01f,
			GameMcpCommands::GetDefaultPlayerRadius(game));
		const Collision::Sphere playerSpawn = {
			DirectX::SimpleMath::Vector3(
				0.0f,
				TableConfig::FIELD_HEIGHT,
				0.0f),
			playerRadius,
		};
		std::vector<Collision::Sphere> placedEnemies;
		placedEnemies.reserve(enemyArray.size());

		for (std::size_t enemyIndex = 0;
			enemyIndex < enemyArray.size();
			enemyIndex++)
		{
			const nlohmann::json& placement =
				enemyArray[enemyIndex];
			if (!placement.is_object() ||
				!placement.contains("enemy_id") ||
				!placement["enemy_id"].is_string() ||
				!placement.contains("x") ||
				!placement["x"].is_number() ||
				!placement.contains("z") ||
				!placement["z"].is_number())
			{
				return CommandResult(
					false,
					"Each enemy placement requires enemy_id, x, and z.");
			}

			const std::string enemyId =
				placement["enemy_id"].get<std::string>();
			const auto definitionIt =
				enemyDefinitions.find(enemyId);
			if (definitionIt == enemyDefinitions.end())
			{
				return CommandResult(
					false,
					"Unknown enemy_id at placement " +
						std::to_string(enemyIndex) +
						": " + enemyId + ".");
			}

			const double requestedX =
				placement["x"].get<double>();
			const double requestedZ =
				placement["z"].get<double>();
			if (!std::isfinite(requestedX) ||
				!std::isfinite(requestedZ))
			{
				return CommandResult(
					false,
					"Enemy coordinates must be finite.");
			}

			EnemySpawnData spawn;
			spawn.enemyId = enemyId;
			spawn.enemyData = definitionIt->second;
			spawn.position =
				DirectX::SimpleMath::Vector3(
					static_cast<float>(requestedX),
					TableConfig::FIELD_HEIGHT,
					static_cast<float>(requestedZ));
			spawn.enemyData.initPosition = spawn.position;

			const float radius = (std::max)(
				0.01f,
				spawn.enemyData.status.radius);
			const float edgeClearance =
				TableConfig::POCKET_RADIUS;
			if (std::abs(spawn.position.x) +
					radius + edgeClearance >
					fieldHalfWidth ||
				std::abs(spawn.position.z) +
					radius + edgeClearance >
					fieldHalfDepth)
			{
				return CommandResult(
					false,
					"Enemy placement " +
						std::to_string(enemyIndex) +
						" is outside the safe playable area.");
			}

			const Collision::Sphere enemySphere = {
				spawn.position,
				radius,
			};
			if (Collision::CheckHit(
				enemySphere,
				playerSpawn))
			{
				return CommandResult(
					false,
					"Enemy placement " +
						std::to_string(enemyIndex) +
						" overlaps the player spawn.");
			}
			for (const Collision::Sphere& placed :
				placedEnemies)
			{
				if (Collision::CheckHit(enemySphere, placed))
				{
					return CommandResult(
						false,
						"Enemy placement " +
							std::to_string(enemyIndex) +
							" overlaps another enemy.");
				}
			}

			placedEnemies.push_back(enemySphere);
			stage.enemies.push_back(std::move(spawn));
		}

		GameMcpCommands::QueueNextStageOverride(game, std::move(stage));
		return CommandResult(
			true,
			"Queued MCP stage layout '" + layoutId +
				"' with " +
				std::to_string(enemyArray.size()) +
				" enemies for the next battle.");
	}

	if (action == "clear_next_stage_layout")
	{
		GameMcpCommands::ClearNextStageOverride(game);
		return CommandResult(
			true,
			"Cleared the queued MCP stage layout.");
	}

	if (action == "start_new_run")
	{
		if (scene != "title" && scene != "result")
		{
			return CommandResult(
				false,
				"A new run can only start from the title or result scene.");
		}
		GameMcpCommands::DisableAutoPlayForExternalControl(game);
		std::optional<std::uint32_t> forcedRandomSeed;
		if (arguments.contains("run_seed"))
		{
			if (!arguments["run_seed"].is_number_unsigned() &&
				!arguments["run_seed"].is_number_integer())
			{
				return CommandResult(false, "run_seed must be an integer.");
			}
			const long long seed = arguments["run_seed"].get<long long>();
			if (seed < 0 || seed > 0xffffffffll)
			{
				return CommandResult(
					false,
					"run_seed must be between 0 and 4294967295.");
			}
			forcedRandomSeed = static_cast<std::uint32_t>(seed);
		}
		const std::string forcedValidationVariant = arguments.value(
			"validation_variant",
			std::string());
		if (!forcedValidationVariant.empty())
		{
			const bool found = GameMcpCommands::HasValidationVariant(
				game,
				forcedValidationVariant);
			if (!found)
			{
				return CommandResult(
					false,
					"validation_variant does not exist.");
			}
		}
		game.StartNewRun(
			"mcp",
			arguments.value(
				"controller_profile",
				std::string("unknown")),
			arguments.value(
				"build_profile",
				std::string("unknown")),
			arguments.value(
				"build_profile_settings_hash",
				std::string()),
			forcedRandomSeed,
			forcedValidationVariant);
		game.ChangeScene(SceneType::Select);
		return CommandResult(true, "Started a new run.");
	}

	if (action == "choose_destination")
	{
		if (scene != "stage_select")
		{
			return CommandResult(
				false,
				"A destination can only be chosen from stage select.");
		}
		StageSelectScene* stageSelect =
			dynamic_cast<StageSelectScene*>(game.GetCurrentScene());
		if (stageSelect == nullptr)
		{
			return CommandResult(
				false,
				"Route options are unavailable in the current scene.");
		}
		const int routeIndex = arguments.value("route_index", -1);
		if (routeIndex < 0 ||
			routeIndex >= stageSelect->GetRouteNodeCount())
		{
			return CommandResult(
				false,
				"route_index must identify one of the current route_options.");
		}
		const std::string selectedDestination =
			stageSelect->GetRouteIdAt(routeIndex);
		if (!stageSelect->ChooseRoute(routeIndex, "mcp"))
		{
			return CommandResult(false, "Failed to enter the selected route.");
		}
		return CommandResult(
			true,
			"Entered route option " + std::to_string(routeIndex) +
			" (" + selectedDestination + ").");
	}

	if (action == "select_ball")
	{
		if (scene != "battle" ||
			game.GetBattleState() != BattleState::AimingDirection)
		{
			return CommandResult(
				false,
				"A ball can only be selected while aiming in battle.");
		}
		const int offerIndex =
			arguments.value("offer_index", -1);
		if (offerIndex < 0 ||
			offerIndex >= game.GetBallOfferCount())
		{
			return CommandResult(
				false,
				"offer_index is outside the available range.");
		}
		game.ApplyBalanceAutoBallSelection(offerIndex);
		recordBuildDecision();
		return CommandResult(true, "Selected the requested ball.");
	}

	if (action == "fire_shot")
	{
		if (scene != "battle" ||
			game.GetBattleState() != BattleState::AimingDirection)
		{
			return CommandResult(
				false,
				"A shot can only be fired while aiming in battle.");
		}
		const std::vector<PlayerBall*> players =
			GameRuntime::World().GetComponents<PlayerBall>();
		if (players.empty() ||
			players[0] == nullptr ||
			!players[0]->IsIdle() ||
			!game.AreAllBallsStopped())
		{
			return CommandResult(
				false,
				"The player ball is not ready to fire.");
		}

		const float directionX =
			arguments.value("direction_x", 0.0f);
		const float directionZ =
			arguments.value("direction_z", 0.0f);
		const float length =
			std::sqrt(
				directionX * directionX +
				directionZ * directionZ);
		if (!std::isfinite(length) || length <= 0.0001f)
		{
			return CommandResult(
				false,
				"The shot direction must be finite and non-zero.");
		}

		const float power = std::clamp(
			arguments.value("power", 0.0f),
			1.0f,
			8.0f);
		if (!std::isfinite(power))
		{
			return CommandResult(
				false,
				"The shot power must be finite.");
		}

		const DirectX::SimpleMath::Vector3 direction(
			directionX / length,
			0.0f,
			directionZ / length);
		const nlohmann::json telemetry =
			arguments.contains("telemetry") &&
			arguments["telemetry"].is_object()
			? arguments["telemetry"]
			: nlohmann::json::object();
		if (!game.TryFireAutomatedShot(
			direction * power,
			-1,
			&telemetry))
		{
			return CommandResult(
				false,
				"The shot is no longer valid for the current battle state.");
		}
		return CommandResult(true, "Fired the requested shot.");
	}

	if (action == "heal")
	{
		if (scene != "rest_site")
		{
			return CommandResult(
				false,
				"Healing is only available at the rest site.");
		}
		if (!game.RestHeal())
		{
			return CommandResult(
				false,
				"Player HP is already full or healing is unavailable.");
		}
		return CommandResult(
			true,
			"Recovered up to " +
			std::to_string(game.GetRestHealPercent()) +
			"% of maximum player HP, capped at maximum HP.");
	}

	if (action == "upgrade_ball")
	{
		if (scene != "rest_site")
		{
			return CommandResult(
				false,
				"Ball upgrades are only available at the rest site.");
		}
		const std::uint64_t instanceId =
			arguments.value(
				"instance_id",
				std::uint64_t{ 0 });
		const int ballIndex =
			FindDeckBallIndex(game, instanceId);
		if (ballIndex < 0 ||
			!game.RestUpgradeBall(ballIndex))
		{
			return CommandResult(
				false,
				"The ball was not found or cannot be upgraded.");
		}
		recordBuildDecision();
		return CommandResult(true, "Upgraded the requested ball.");
	}

	if (action == "remove_ball")
	{
		if (scene != "shop")
		{
			return CommandResult(
				false,
				"Balls can only be removed at the shop.");
		}
		const std::uint64_t instanceId =
			arguments.value(
				"instance_id",
				std::uint64_t{ 0 });
		const int ballIndex =
			FindDeckBallIndex(game, instanceId);
		if (ballIndex < 0 ||
			!game.RemoveShopBall(
				ballIndex,
				kShopRemoveCost))
		{
			return CommandResult(
				false,
				"The ball cannot be removed. Keep at least 5 balls and check money.");
		}
		return CommandResult(true, "Removed the requested ball.");
	}

	if (action == "buy_relic")
	{
		if (scene != "shop")
		{
			return CommandResult(
				false,
				"Relics can only be purchased at the shop.");
		}

		const int relicIndex =
			arguments.value("relic_index", -1);
		const RelicDefinition* relic = game.GetRelic(relicIndex);
		if (relic == nullptr)
		{
			return CommandResult(
				false,
				"relic_index is outside the available range.");
		}
		if (game.HasRelic(relic->type))
		{
			return CommandResult(
				false,
				"The requested relic is already owned.");
		}
		if (!game.IsShopRelicOffered(relicIndex))
		{
			return CommandResult(
				false,
				"The requested relic is not one of this shop's offers.");
		}
		if (!game.BuyShopRelic(relicIndex))
		{
			return CommandResult(
				false,
				"The requested relic could not be purchased. Check money, ownership and the current offers.");
		}
		recordBuildDecision();

		return CommandResult(
			true,
			std::string("Purchased relic: ") + relic->name + ".");
	}

	if (action == "choose_relic")
	{
		if (!game.IsClearRewardActive() ||
			!game.IsMidBossRelicSelectionActive())
		{
			return CommandResult(
				false,
				"A midboss relic choice is not currently available.");
		}
		const int relicIndex = arguments.value("relic_index", -1);
		const RelicDefinition* relic = game.GetRelic(relicIndex);
		if (relic == nullptr || !game.AcquireMidBossRelic(relicIndex))
		{
			return CommandResult(
				false,
				"Choose an unowned relic from the current midboss offers.");
		}
		recordBuildDecision();
		return CommandResult(
			true,
			std::string("Acquired midboss relic: ") + relic->name + ".");
	}

	if (action == "continue_to_battle")
	{
		if (scene != "rest_site" && scene != "shop")
		{
			return CommandResult(
				false,
				"This action is only available at a rest site or shop.");
		}
		if (scene == "rest_site")
		{
			RestSiteScene* restSite =
				dynamic_cast<RestSiteScene*>(game.GetCurrentScene());
			if (restSite != nullptr &&
				!restSite->HasUsedAction() &&
				game.HasAvailableRestBenefit())
			{
				return CommandResult(
					false,
					"Choose heal or upgrade_ball before leaving the rest site.");
			}
		}
		if (scene == "rest_site")
		{
			game.LeaveRestSite();
		}
		else
		{
			game.LeaveShop();
		}
		return CommandResult(true, "Completed the area and returned to the route.");
	}

	if (action == "choose_reward")
	{
		if (!game.IsClearRewardActive() ||
			game.IsClearRewardChosen() ||
			game.IsMidBossRelicSelectionActive())
		{
			return CommandResult(
				false,
				"A clear reward is not currently available.");
		}

		const int moneyBefore = game.GetPlayerMoney();
		const std::string reward =
			arguments.value("reward", std::string());
		bool applied = false;
		int chargedUpgradeCost = 0;
		int selectedOfferIndex = -1;
		int selectedCatalogIndex = -1;
		std::string selectedBallId;
		if (reward == "new_ball")
		{
			selectedOfferIndex = arguments.value("offer_index", -1);
			if (selectedOfferIndex < 0)
			{
				// Backward compatibility: a catalog index is accepted only when
				// that definition is one of the three currently offered slots.
				const int requestedCatalogIndex =
					arguments.value("catalog_index", -1);
				for (int offerIndex = 0;
					offerIndex < game.GetClearRewardBallOfferCount();
					offerIndex++)
				{
					if (game.GetClearRewardBallOfferCatalogIndex(offerIndex) ==
						requestedCatalogIndex)
					{
						selectedOfferIndex = offerIndex;
						break;
					}
				}
			}
			selectedCatalogIndex =
				game.GetClearRewardBallOfferCatalogIndex(selectedOfferIndex);
			const PlayerBallData* selectedBall =
				game.GetClearRewardBallOffer(selectedOfferIndex);
			if (selectedBall == nullptr)
			{
				return CommandResult(
					false,
					"offer_index must identify a current clear_reward_ball_offers entry.");
			}
			selectedBallId = selectedBall->definitionId;
			applied = game.AddClearRewardBallOffer(selectedOfferIndex);
			if (applied)
			{
				game.NotifyBallAcquired(selectedBallId);
			}
		}
		else if (reward == "upgrade_ball")
		{
			const std::uint64_t instanceId =
				arguments.value(
					"instance_id",
					std::uint64_t{ 0 });
			const int ballIndex =
				FindDeckBallIndex(game, instanceId);
			const int upgradeCost = ballIndex >= 0
				? game.GetClearRewardUpgradeCost(ballIndex)
				: -1;
			if (upgradeCost >= 0 &&
				game.GetPlayerMoney() < upgradeCost)
			{
				return CommandResult(
					false,
					"Not enough Money for the requested clear-reward upgrade.");
			}
			applied = ballIndex >= 0 &&
				game.ApplyClearRewardUpgrade(
					ballIndex,
					chargedUpgradeCost);
			selectedOfferIndex = ballIndex;
		}
		else if (reward == "extra_money")
		{
			selectedOfferIndex = 0;
			applied = GameMcpCommands::ApplyExtraMoneyReward(
				game,
				kExtraRewardMoney);
		}
		else
		{
			return CommandResult(
				false,
				"reward must be new_ball, upgrade_ball, or extra_money.");
		}

		if (!applied)
		{
			return CommandResult(
				false,
				"The requested clear reward could not be applied.");
		}
		const int rewardIndex = reward == "new_ball"
			? 0
			: reward == "upgrade_ball" ? 1 : 2;
		game.MarkBalanceAutoRewardChosen(
			rewardIndex,
			selectedOfferIndex,
			"External AI selected the clear reward.");
		nlohmann::json rewardDetails = {
			{ "controller", "mcp" },
			{ "reward", reward },
			{ "selected_index", selectedOfferIndex },
			{ "money_before", moneyBefore },
			{ "money_after", game.GetPlayerMoney() },
		};
		if (reward == "upgrade_ball")
		{
			rewardDetails["upgrade_cost"] = chargedUpgradeCost;
		}
		else if (reward == "new_ball")
		{
			rewardDetails["offer_index"] = selectedOfferIndex;
			rewardDetails["catalog_index"] = selectedCatalogIndex;
			rewardDetails["ball_id"] = selectedBallId;
		}
		game.RecordBalanceEvent(
			"clear_reward_choice",
			rewardDetails);
		recordBuildDecision();
		return CommandResult(true, "Applied the requested clear reward.");
	}

	if (action == "continue_after_reward")
	{
		if (!game.IsClearRewardActive() ||
			!game.IsClearRewardChosen())
		{
			return CommandResult(
				false,
				"Choose a clear reward before continuing.");
		}
		game.ContinueAfterClearReward();
		return CommandResult(true, "Returned to stage select.");
	}

	return CommandResult(false, "Unknown game-control action.");
}

void GameMcpBridge::ProcessPendingCommand(Game& game)
{
	const std::filesystem::path commandPath =
		GetCommandPath();
	if (!std::filesystem::exists(commandPath))
	{
		return;
	}

	nlohmann::json command;
	if (!ReadJson(commandPath, command))
	{
		return;
	}

	const std::string commandId =
		command.value("command_id", std::string());
	if (commandId.empty() ||
		commandId == m_LastCommandId)
	{
		std::error_code error;
		std::filesystem::remove(commandPath, error);
		return;
	}
	m_LastCommandId = commandId;

	nlohmann::json result;
	try
	{
		result = ExecuteCommand(game, command);
	}
	catch (const nlohmann::json::exception& error)
	{
		result = CommandResult(
			false,
			std::string("Invalid command arguments: ") +
				error.what());
	}

	PublishState(game, true);
	m_FramesUntilPublish = m_PublishIntervalFrames;
	result["schema_version"] = 1;
	result["command_id"] = commandId;
	result["action"] =
		command.value("action", std::string());
	result["completed_at_unix_ms"] =
		UnixTimeMilliseconds();
	result["state_sequence"] = m_StateSequence;

	if (!WriteJsonAtomically(GetResultPath(), result))
	{
		std::cerr
			<< "[GameMcpBridge] Failed to publish command result"
			<< std::endl;
		return;
	}

	std::error_code error;
	std::filesystem::remove(commandPath, error);
}
