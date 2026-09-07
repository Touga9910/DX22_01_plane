#include "GameMcpBridge.h"
#include "BallStatusJson.h"

#include "EnemyBall.h"
#include "BreakBall.h"
#include "Game.h"
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

	const char* GetGameStateName(GameState state)
	{
		switch (state)
		{
		case GameState::AimingDirection: return "aiming_direction";
		case GameState::AimingPower: return "aiming_power";
		case GameState::ConfirmShot: return "confirm_shot";
		case GameState::BallsMoving: return "balls_moving";
		case GameState::EnemyAttack: return "enemy_attack";
		case GameState::TurnEnd: return "turn_end";
		case GameState::ClearReward: return "clear_reward";
		case GameState::GameOver: return "game_over";
		default: return "unknown";
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

		return {
			{ "layout_id", stage.id },
			{ "stage_type", StageTypeToMcpString(stage.stageType) },
			{ "difficulty", stage.difficulty },
			{ "par", stage.par },
			{ "enemies", std::move(enemies) },
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
			{ "instance_id", ball.instanceId },
			{ "upgrade_level", ball.upgradeLevel },
			{ "can_upgrade", ball.CanUpgrade() },
			{ "next_upgrade", ball.CanUpgrade() ? WriteBallStatus(ball.upgradeTable[ball.upgradeLevel]) : nlohmann::json(nullptr) },
			{ "status", {
				{ "attack", ball.status.attack },
				{ "defense", ball.status.defense },
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
				{ "pierce", ball.status.abilities.pierce },
				{ "split", ball.status.abilities.split },
				{ "anchor", ball.status.abilities.anchor },
			} },
		};
	}

	int FindDeckBallIndex(
		const PlayerDeck& deck,
		std::uint64_t instanceId)
	{
		for (int index = 0;
			index < deck.GetRewardTargetCount();
			index++)
		{
			const PlayerBallData* ball =
				deck.GetRewardTarget(index);
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
	nlohmann::json state = {
		{ "schema_version", 1 },
		{ "running", running },
		{ "published_at_unix_ms", UnixTimeMilliseconds() },
		{ "sequence", m_StateSequence },
		{ "scene", GetSceneName(game.m_Scene) },
		{ "game_state", GetGameStateName(game.m_GameState) },
		{ "selected_stage_id",
			game.m_PlayerRunStatus.GetSelectedStageId() },
		{ "autoplay_enabled",
			game.m_BalanceAutoPlayEnabled },
		{ "bridge", {
			{ "write_actions_enabled", m_AllowWriteActions },
			{ "single_pending_command", true },
		} },
	};

	state["physics_clock"] = {
		{ "model", BallPhysicsWorld::ModelName },
        { "trajectory_prediction", "shared_ccd_toi_v1" },
		{ "substeps_last_tick", game.m_PhysicsSubstepsLastTick },
        { "max_toi_iterations_per_tick", ContinuousBallStepper::MaxIterations },
		{ "substep_limit_count", game.m_PhysicsSubstepLimitCount },
		{ "frequency_hz", 60 },
		{ "tick", game.m_PhysicsTickCount },
		{ "steps_last_frame", game.m_PhysicsStepsLastFrame },
		{ "max_steps_per_frame", FixedStepClock::MaxStepsPerFrame },
		{ "dropped_seconds", game.m_PhysicsClock.DroppedSeconds() },
	};
	state["table"] = {
		{ "field_width", TableConfig::GetFieldWidth() },
		{ "field_depth", TableConfig::GetFieldDepth() },
		{ "walls", nlohmann::json::array() },
		{ "pockets", nlohmann::json::array() },
	};
	state["rest_heal"] = {
		{ "heal_ratio", game.m_RestHealRatio },
		{ "heal_percent", game.GetRestHealPercent() },
		{ "configured_heal_amount", game.GetRestHealAmount() },
		{ "capped_at_max_hp", true },
		{ "available", game.CanRestHeal() },
	};
	state["meta_progression"] = {
		{"active_ascension", game.m_ActiveAscension},
		{"selected_ascension", game.m_ProgressionProfile.selectedAscension},
		{"highest_unlocked_ascension", game.m_ProgressionProfile.highestUnlockedAscension},
		{"total_runs", game.m_ProgressionProfile.totalRuns},
		{"total_clears", game.m_ProgressionProfile.totalClears},
		{"persistent_rewards_eligible", game.m_PersistentProgressEligible},
		{"enemy_hp_multiplier", ProgressionProfile::EnemyHpMultiplier(game.m_ActiveAscension)},
		{"enemy_attack_bonus", ProgressionProfile::EnemyAttackBonus(game.m_ActiveAscension)},
	};
	state["meta_progression"]["ascension_rules"] = nlohmann::json::array();
	for (int level = 0; level <= ProgressionProfile::MaximumAscension; ++level)
	{
		state["meta_progression"]["ascension_rules"].push_back({
			{ "level", level },
			{ "rule", ProgressionProfile::AscensionRule(level) },
			{ "unlocked", level <= game.m_ProgressionProfile.highestUnlockedAscension },
		});
	}
	state["meta_progression"]["achievements"] = nlohmann::json::array();
	for (const AchievementDefinition& achievement : AchievementCatalog)
	{
		state["meta_progression"]["achievements"].push_back({
			{ "key", achievement.key },
			{ "name", achievement.name },
			{ "condition", achievement.condition },
			{ "reward", achievement.reward },
			{ "unlocked", game.m_ProgressionProfile.IsAchievementUnlocked(achievement.id) },
		});
	}
	state["meta_progression"]["ball_unlocks"] = {
		{{ "definition_id", "player_standard" }, { "unlocked", true }},
		{{ "definition_id", "player_heavy" }, { "unlocked", true }},
		{{ "definition_id", "player_pierce" }, { "unlocked", game.m_ProgressionProfile.IsBallUnlocked("player_pierce") }},
		{{ "definition_id", "player_bounce" }, { "unlocked", game.m_ProgressionProfile.IsBallUnlocked("player_bounce") }},
		{{ "definition_id", "player_anchor" }, { "unlocked", game.m_ProgressionProfile.IsBallUnlocked("player_anchor") }},
	};
	const std::vector<TableFrame*> tableFrames =
		game.GetComponents<TableFrame>();
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
	const std::vector<Pocket*> pockets = game.GetComponents<Pocket>();
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
			{ "max_hp_damage_ratio", game.m_PlayerPocketDamageRatio },
			{ "damage_amount", game.GetPlayerPocketDamageAmount() },
			{ "damage_ignores_defense", true },
			{ "return_region", {
				{ "center", VectorToJson(
					DirectX::SimpleMath::Vector3(
						0.0f, TableConfig::FIELD_HEIGHT, 0.0f)) },
				{ "half_width", game.m_PlayerPocketReturnHalfWidth },
				{ "half_depth", game.m_PlayerPocketReturnHalfDepth },
			} },
		} },
		{ "enemy", {
			{ "high_hp_result", "skip_attack_then_queue_return" },
			{ "returns_per_turn", 1 },
			{ "return_position", VectorToJson(
				DirectX::SimpleMath::Vector3(
					game.m_EnemyPocketReturnX,
					TableConfig::FIELD_HEIGHT,
					TableConfig::GetFieldDepth() * 0.5f -
						game.m_EnemyPocketReturnTopEdgeOffset)) },
			{ "finisher_hp_ratios", {
				{ "normal", game.m_NormalPocketFinisherRatio },
				{ "midboss", game.m_MidBossPocketFinisherRatio },
				{ "boss", game.m_BossPocketFinisherRatio },
			} },
			{ "current_stage_type",
				StageTypeToMcpString(game.m_CurrentBattleStageType) },
			{ "current_finisher_hp_ratio",
				game.GetCurrentPocketFinisherRatio() },
			{ "return_queue_size", game.m_PocketedEnemyQueue.size() },
		} },
	};

	state["stage_layout_control"] = {
		{ "application_timing", "next_battle_spawn" },
		{ "maximum_enemy_count", kMaximumMcpStageEnemyCount },
		{ "allowed_enemy_ids", m_StageEnemyIds },
		{ "queued_override",
			game.m_McpNextStageOverride.has_value()
				? StageDataToJson(game.m_McpNextStageOverride.value())
				: nlohmann::json(nullptr) },
		{ "current_override",
			game.m_McpCurrentStageOverride.has_value()
				? StageDataToJson(game.m_McpCurrentStageOverride.value())
				: nlohmann::json(nullptr) },
	};

	const std::vector<PlayerBall*> players =
		game.GetComponents<PlayerBall>();
	const PlayerBall* player =
		players.empty() ? nullptr : players[0];
	state["player"] = {
		{ "current_hp",
			player == nullptr
				? game.m_PlayerRunStatus.currentHp
				: player->GetHP() },
		{ "max_hp",
			player == nullptr
				? game.m_PlayerRunStatus.maxHp
				: player->GetMaxHP() },
		{ "money", game.m_PlayerRunStatus.money },
		{ "progress", game.m_PlayerRunStatus.progress },
		{ "pocketed", player != nullptr && player->IsPocketed() },
	};
	const RunResultSnapshot& runStatistics = game.m_RunStatistics.GetState();
	state["run_progress"] = {
		{ "phase", ToString(game.m_RunPhase) },
		{ "area_progress", game.m_AreaProgress },
		{ "area_goal", Game::kNormalRouteAreaGoal },
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
		state["player"]["defense"] = player->GetDefense();
		state["player"]["idle"] = player->IsIdle();
		if (player->GetBall() != nullptr)
		{
			state["player"]["radius"] =
				player->GetBall()->GetRadius();
		}
		const PlayerBallData* selectedBall = game.m_PlayerDeck.GetOffer(game.m_SelectedOfferIndex);
		if (selectedBall == nullptr) selectedBall = game.m_PlayerDeck.GetCurrent();
		if (selectedBall != nullptr)
		{
			state["player"]["ball"] = BallDataToJson(*selectedBall, game.m_SelectedOfferIndex);
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
			{ "unlocked", game.m_ProgressionProfile.IsRelicUnlocked(relic->type) },
			{ "shop_offered", game.IsShopRelicOffered(index) },
			{ "midboss_offered", std::find(
				game.m_MidBossRelicOffers.begin(),
				game.m_MidBossRelicOffers.end(), index) !=
				game.m_MidBossRelicOffers.end() },
		});
	}
	state["relic_selection"] = {
		{ "shop_offer_count", game.GetShopRelicOfferCount() },
		{ "shop_purchase_limited", false },
		{ "shop_purchase_used", false },
		{ "midboss_active", game.m_IsMidBossRelicSelectionActive },
		{ "midboss_offer_count", game.GetMidBossRelicOfferCount() },
	};
	state["relic_effects"] = {
		{ "all_ball_attack_bonus", game.GetRelicAttackBonus() },
		{ "all_ball_defense_bonus", game.GetRelicDefenseBonus() },
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
    for (auto* neutral : game.GetComponents<BreakBall>())
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
		game.GetComponents<EnemyBall>();
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
		index < game.m_PlayerDeck.GetOfferCount();
		index++)
	{
		const PlayerBallData* ball =
			game.m_PlayerDeck.GetOffer(index);
		if (ball == nullptr)
		{
			continue;
		}
		nlohmann::json ballJson =
			BallDataToJson(*ball, index);
		ballJson["selected"] =
			index == game.m_SelectedOfferIndex;
		ballJson["held"] =
			index == game.m_SelectedHoldIndex;
		state["offered_balls"].push_back(
			std::move(ballJson));
	}

	state["deck_balls"] = nlohmann::json::array();
	for (int index = 0;
		index < game.m_PlayerDeck.GetRewardTargetCount();
		index++)
	{
		const PlayerBallData* ball =
			game.m_PlayerDeck.GetRewardTarget(index);
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
				game.m_PlayerRunStatus.money >= upgradeCost;
			state["deck_balls"].push_back(
				std::move(ballJson));
		}
	}

	state["catalog_balls"] = nlohmann::json::array();
	for (int index = 0;
		index < game.m_PlayerDeck.GetCatalogCount();
		index++)
	{
		const PlayerBallData* ball =
			game.m_PlayerDeck.GetCatalogBall(index);
		if (ball != nullptr)
		{
			state["catalog_balls"].push_back(
				BallDataToJson(*ball, index));
		}
	}

	const int deckBallCount =
		game.m_PlayerDeck.GetRewardTargetCount();
	state["deck_rule"] = {
		{ "minimum_size", PlayerDeck::MinimumDeckSize },
		{ "current_size", deckBallCount },
		{ "shop_removal_cost", kShopRemoveCost },
		{ "can_remove",
			deckBallCount > PlayerDeck::MinimumDeckSize &&
			game.m_PlayerRunStatus.money >= kShopRemoveCost },
	};

	game.PruneBalanceAutoPendingBalls();
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
	for (const std::uint64_t instanceId :
		game.m_AutoPendingBallAdjustments)
	{
		const int ballIndex =
			FindDeckBallIndex(game.m_PlayerDeck, instanceId);
		const PlayerBallData* ball =
			ballIndex < 0
				? nullptr
				: game.m_PlayerDeck.GetRewardTarget(ballIndex);
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

	const int dynamicHpDelta =
		game.m_DynamicBalanceEnabled
			? game.m_DynamicBalanceLevel *
				game.m_DynamicBalanceHpStep
			: 0;
	const int dynamicAttackDelta =
		game.m_DynamicBalanceEnabled
			? game.CalculateDynamicBalanceAttackModifier(
				game.m_DynamicBalanceLevel)
			: 0;
	state["dynamic_balance"] = {
		{ "role", "assist_mode" },
		{ "enabled", game.m_DynamicBalanceEnabled },
		{ "current_battle_enabled",
			game.m_DynamicBalanceAppliedEnabled },
		{ "level", game.m_DynamicBalanceLevel },
		{ "current_battle_level",
			game.m_DynamicBalanceAppliedLevel },
		{ "minimum_level", game.m_DynamicBalanceMinLevel },
		{ "maximum_level", game.m_DynamicBalanceMaxLevel },
		{ "application_timing", "next_battle_spawn" },
		{ "next_enemy_modifier", {
			{ "max_hp_delta", dynamicHpDelta },
			{ "attack_delta", dynamicAttackDelta },
		} },
		{ "current_stage_metrics", {
			{ "active", game.m_DynamicBalanceStageActive },
			{ "shots", game.m_DynamicBalanceStageShots },
			{ "no_hit_shots",
				game.m_DynamicBalanceStageNoHitShots },
			{ "enemy_count",
				game.m_DynamicBalanceStageEnemyCount },
		} },
		{ "last_evaluation", {
			{ "result", game.m_DynamicBalanceLastResult },
			{ "reason", game.m_DynamicBalanceLastReason },
			{ "level_change",
				game.m_DynamicBalanceLastLevelChange },
			{ "remaining_hp_ratio",
				game.m_DynamicBalanceLastHpRatio },
			{ "no_hit_rate",
				game.m_DynamicBalanceLastNoHitRate },
			{ "shots_per_enemy",
				game.m_DynamicBalanceLastShotsPerEnemy },
		} },
	};
	state["progression_scaling"] = {
		{ "enabled", game.m_ProgressionScalingEnabled },
		{ "current_progress", game.m_PlayerRunStatus.progress },
		{ "next_enemy_hp_delta",
			game.CalculateProgressionHpModifier() },
		{ "hp_start_progress", game.m_ProgressionHpStart },
		{ "hp_interval", game.m_ProgressionHpInterval },
		{ "hp_step", game.m_ProgressionHpStep },
		{ "maximum_hp_delta",
			game.m_ProgressionHpMaximumDelta },
		{ "next_enemy_attack_delta",
			game.CalculateProgressionAttackModifier() },
		{ "attack_start_progress", game.m_ProgressionAttackStart },
		{ "attack_interval", game.m_ProgressionAttackInterval },
		{ "attack_step", game.m_ProgressionAttackStep },
		{ "maximum_attack_delta",
			game.m_ProgressionAttackMaximumDelta },
	};
	state["baseline_difficulty"] = {
		{ "profile", game.m_BaselineDifficultyProfile },
		{ "enemy_hp_multiplier", game.m_BaselineEnemyHpMultiplier },
		{ "enemy_attack_delta", game.m_BaselineEnemyAttackDelta },
	};
	state["balance_validation"] = {
		{ "enabled", game.m_BalanceValidationEnabled },
		{ "experiment_id", game.m_BalanceValidationExperimentId },
		{ "variant_id", game.m_BalanceValidationCurrentVariantId },
		{ "variant_index", game.m_BalanceValidationVariantIndex },
		{ "variant_count", game.m_BalanceValidationVariants.size() },
		{ "run_seed", game.m_RunRandomSeed },
		{ "stage_selection_seed", game.m_StageSelectionSeed },
		{ "route_selection_seed", game.m_RouteSelectionSeed },
		{ "seed_suite_index", game.m_BalanceValidationSeedIndex },
		{ "seed_suite_size", game.m_BalanceValidationSeeds.size() },
		{ "maximum_cleared_stages",
			game.m_BalanceValidationMaximumClearedStages },
		{ "endurance_mode", game.m_BalanceValidationEnduranceMode },
		{ "cleared_stage_count", game.m_ClearedStageCount },
		{
			"dynamic_balance_forced_off",
			game.m_BalanceValidationEnabled &&
			game.m_BalanceValidationCurrentDisableDynamicBalance
		},
	};

	state["available_actions"] = nlohmann::json::array();
	state["route_options"] = nlohmann::json::array();
	state["run_map"] = game.m_RunMap.Snapshot();
	if (dynamic_cast<StageSelectScene*>(game.m_Scene) == nullptr)
		for (auto& node : state["run_map"]["nodes"]) node["selectable"] = false;
	state["available_actions"].push_back("set_dynamic_balance");
	state["available_actions"].push_back("set_next_stage_layout");
	if (game.m_McpNextStageOverride.has_value())
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
			dynamic_cast<StageSelectScene*>(game.m_Scene);
		if (stageSelect != nullptr)
		{
			for (int routeIndex = 0;
				routeIndex < stageSelect->GetRouteNodeCount();
				routeIndex++)
			{
				state["route_options"].push_back({
					{ "route_index", routeIndex },
					{ "node_id", stageSelect->GetMapNodeIdAt(routeIndex) },
					{ "next_node_ids", game.m_RunMap.Node(stageSelect->GetMapNodeIdAt(routeIndex))->next },
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
			dynamic_cast<RestSiteScene*>(game.m_Scene);
		const bool actionUsed =
			restSite != nullptr && restSite->HasUsedAction();
		const bool canHeal = game.CanRestHeal();
		bool canUpgrade = false;
		for (int index = 0;
			index < game.m_PlayerDeck.GetRewardTargetCount();
			index++)
		{
			const PlayerBallData* ball =
				game.m_PlayerDeck.GetRewardTarget(index);
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
			{ "heal_ratio", game.m_RestHealRatio },
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
				game.m_PlayerRunStatus.money >= relic->price)
			{
				state["available_actions"].push_back(
					"buy_relic");
				break;
			}
		}
		if (game.m_PlayerRunStatus.money >= kShopRemoveCost &&
			game.m_PlayerDeck.GetRewardTargetCount() >
				PlayerDeck::MinimumDeckSize)
		{
			state["available_actions"].push_back(
				"remove_ball");
		}
		state["available_actions"].push_back(
			"continue_to_battle");
	}
	else if (scene == "battle" &&
		game.m_GameState == GameState::AimingDirection &&
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

	if (game.m_GameState == GameState::ClearReward)
	{
		state["clear_reward_rule"] = {
			{ "upgrade_requires_money", true },
			{ "upgrade_cost_by_current_level", {
				{ "0", 15 },
				{ "1", 30 },
			} },
			{ "rest_site_upgrade_requires_money", false },
		};
		if (game.m_IsMidBossRelicSelectionActive)
		{
			state["available_actions"].push_back("choose_relic");
		}
		else if (game.m_IsClearRewardChosen)
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

	state["debug_mode"] = {{"active", game.m_DebugMode}, {"editor_open", game.m_DebugEditorOpen},
		{"finished", game.m_DebugBattleFinished}};
	if (!game.m_StageEditor.draft.is_null())
	{
		state["stage_editor"] = game.m_StageEditor.Snapshot(game.m_DebugEnemyCatalog, game.StageEditorPlayerRadius());
		state["stage_editor"]["open"] = game.m_DebugEditorOpen;
	}
	if (game.m_DebugEditorOpen) state["available_actions"] = {"validate_stage_layout", "propose_stage_layout"};
	else if (game.m_DebugMode)
	{
		auto allowed = nlohmann::json::array();
		for (const auto& action : state["available_actions"])
			if (action == "select_ball" || action == "fire_shot" || action == "evaluate_boss_shots" || action == "fire_boss_shot")
				allowed.push_back(action);
		state["available_actions"] = std::move(allowed);
	}
	if (dynamic_cast<TitleScene*>(game.m_Scene) != nullptr && !game.m_DebugEditorOpen)
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
		if (dynamic_cast<TitleScene*>(game.m_Scene) == nullptr) return CommandResult(false, "Open the stage editor from the title screen.");
		game.OpenDebugMode();
		return CommandResult(true, "Stage editor opened in debug setup.");
	}
	if (action == "validate_stage_layout" || action == "propose_stage_layout")
	{
		if (!game.m_DebugEditorOpen) return CommandResult(false, "Open the stage editor first.");
		const auto args = command.value("arguments", nlohmann::json::object());
		const auto layout = args.value("layout", nlohmann::json());
		const auto report = StageLayoutEditor::Inspect(layout, game.m_DebugEnemyCatalog, game.StageEditorPlayerRadius());
		auto result = CommandResult(report["valid"].get<bool>(), "Layout validation complete.");
		result["report"] = report;
		if (action == "propose_stage_layout" && report["valid"].get<bool>())
		{
			try
			{
				const auto& expected = args.at("expected_revision");
				if (!expected.is_number_integer() || expected.get<double>() < 0) throw std::runtime_error("expected_revision must be a nonnegative integer.");
				game.m_StageEditor.Propose(layout, expected.get<std::uint64_t>(), game.m_DebugEnemyCatalog, game.StageEditorPlayerRadius());
				result["message"] = "Proposal ready for visual review. Draft and files are unchanged until accepted in the editor.";
			}
			catch (const std::exception& e) { result = CommandResult(false, e.what()); result["report"] = report; }
		}
		return result;
	}
	if (game.m_DebugEditorOpen)
		return CommandResult(false, "Debug setup is open. Resume or start the battle from its window.");
	if (game.m_DebugMode && action != "select_ball" && action != "fire_shot" &&
		action != "evaluate_boss_shots" && action != "fire_boss_shot")
		return CommandResult(false, "This action is unavailable in debug battle mode.");
	const nlohmann::json arguments =
		command.value(
			"arguments",
			nlohmann::json::object());
	const std::string scene = GetSceneName(game.m_Scene);
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
        if (scene != "battle" || game.GetGameState() != GameState::AimingDirection || !game.AreAllBallsStopped())
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
	if (action == "set_dynamic_balance")
	{
		const bool enabled = arguments.value(
			"enabled",
			game.m_DynamicBalanceEnabled);
		const bool resetLevel =
			arguments.value("reset_level", false);
		const bool hasRequestedLevel =
			arguments.contains("level") &&
			arguments["level"].is_number_integer();
		if (arguments.contains("level") &&
			!hasRequestedLevel)
		{
			return CommandResult(
				false,
				"level must be an integer when provided.");
		}
		const int requestedLevel =
			hasRequestedLevel
				? arguments["level"].get<int>()
				: game.m_DynamicBalanceLevel;
		game.SetDynamicBalance(
			enabled,
			resetLevel,
			requestedLevel,
			hasRequestedLevel);
		return CommandResult(
			true,
			std::string("Dynamic balance ") +
				(enabled ? "enabled" : "disabled") +
				". Level=" +
				std::to_string(game.m_DynamicBalanceLevel) +
				". Changes apply to newly spawned enemies.");
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
			game.m_DefaultPlayerStatus.radius);
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

		game.m_McpNextStageOverride = std::move(stage);
		return CommandResult(
			true,
			"Queued MCP stage layout '" + layoutId +
				"' with " +
				std::to_string(enemyArray.size()) +
				" enemies for the next battle.");
	}

	if (action == "clear_next_stage_layout")
	{
		game.m_McpNextStageOverride.reset();
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
		game.m_BalanceAutoPlayEnabled = false;
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
			const bool found = std::any_of(
				game.m_BalanceValidationVariants.begin(),
				game.m_BalanceValidationVariants.end(),
				[&forcedValidationVariant](
					const BalanceValidationVariant& variant)
				{
					return variant.id == forcedValidationVariant;
				});
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
		game.m_GameState = GameState::AimingDirection;
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
			dynamic_cast<StageSelectScene*>(game.m_Scene);
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
			game.m_GameState != GameState::AimingDirection)
		{
			return CommandResult(
				false,
				"A ball can only be selected while aiming in battle.");
		}
		const int offerIndex =
			arguments.value("offer_index", -1);
		if (offerIndex < 0 ||
			offerIndex >= game.m_PlayerDeck.GetOfferCount())
		{
			return CommandResult(
				false,
				"offer_index is outside the available range.");
		}
		game.m_SelectedOfferIndex = offerIndex;
		if (game.m_SelectedHoldIndex == offerIndex)
		{
			game.m_SelectedHoldIndex = -1;
		}
		game.ApplySelectedBallPreview();
		recordBuildDecision();
		return CommandResult(true, "Selected the requested ball.");
	}

	if (action == "fire_shot")
	{
		if (scene != "battle" ||
			game.m_GameState != GameState::AimingDirection)
		{
			return CommandResult(
				false,
				"A shot can only be fired while aiming in battle.");
		}
		const std::vector<PlayerBall*> players =
			game.GetComponents<PlayerBall>();
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
		game.m_PendingShotTelemetry =
			arguments.contains("telemetry") &&
			arguments["telemetry"].is_object()
			? arguments["telemetry"]
			: nlohmann::json::object();
		players[0]->FireAutomatedShot(direction * power);
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
			FindDeckBallIndex(
				game.m_PlayerDeck,
				instanceId);
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
			FindDeckBallIndex(
				game.m_PlayerDeck,
				instanceId);
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
		if (game.m_GameState != GameState::ClearReward ||
			!game.m_IsMidBossRelicSelectionActive)
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
				dynamic_cast<RestSiteScene*>(game.m_Scene);
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
		if (game.m_GameState != GameState::ClearReward ||
			game.m_IsClearRewardChosen ||
			game.m_IsMidBossRelicSelectionActive)
		{
			return CommandResult(
				false,
				"A clear reward is not currently available.");
		}

		const int moneyBefore = game.m_PlayerRunStatus.money;
		const std::string reward =
			arguments.value("reward", std::string());
		bool applied = false;
		int chargedUpgradeCost = 0;
		if (reward == "new_ball")
		{
			const int catalogIndex =
				arguments.value("catalog_index", -1);
			applied =
				game.m_PlayerDeck.AddCatalogBall(catalogIndex);
			game.m_SelectedRewardIndex = 0;
			game.m_SelectedRewardBallIndex = catalogIndex;
		}
		else if (reward == "upgrade_ball")
		{
			const std::uint64_t instanceId =
				arguments.value(
					"instance_id",
					std::uint64_t{ 0 });
			const int ballIndex =
				FindDeckBallIndex(
					game.m_PlayerDeck,
					instanceId);
			const int upgradeCost = ballIndex >= 0
				? game.GetClearRewardUpgradeCost(ballIndex)
				: -1;
			if (upgradeCost >= 0 &&
				game.m_PlayerRunStatus.money < upgradeCost)
			{
				return CommandResult(
					false,
					"Not enough Money for the requested clear-reward upgrade.");
			}
			applied = ballIndex >= 0 &&
				game.ApplyClearRewardUpgrade(
					ballIndex,
					chargedUpgradeCost);
			game.m_SelectedRewardIndex = 1;
			game.m_SelectedRewardBallIndex = ballIndex;
		}
		else if (reward == "extra_money")
		{
			game.m_PlayerRunStatus.money +=
				kExtraRewardMoney;
			game.m_SelectedRewardIndex = 2;
			game.m_SelectedRewardBallIndex = 0;
			applied = true;
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
		game.m_IsClearRewardChosen = true;
		game.m_RewardMessage =
			"External AI selected the clear reward.";
		nlohmann::json rewardDetails = {
			{ "controller", "mcp" },
			{ "reward", reward },
			{ "selected_index", game.m_SelectedRewardBallIndex },
			{ "money_before", moneyBefore },
			{ "money_after", game.m_PlayerRunStatus.money },
		};
		if (reward == "upgrade_ball")
		{
			rewardDetails["upgrade_cost"] = chargedUpgradeCost;
		}
		game.RecordBalanceEvent(
			"clear_reward_choice",
			rewardDetails);
		recordBuildDecision();
		return CommandResult(true, "Applied the requested clear reward.");
	}

	if (action == "continue_after_reward")
	{
		if (game.m_GameState != GameState::ClearReward ||
			!game.m_IsClearRewardChosen)
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
