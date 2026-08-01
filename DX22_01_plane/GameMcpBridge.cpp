#include "GameMcpBridge.h"

#include "EnemyBall.h"
#include "Game.h"
#include "PlayerBall.h"
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
				{ "max_hp", spawn.enemyData.status.maxHp },
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
			{ "status", {
				{ "max_hp", ball.status.maxHp },
				{ "attack", ball.status.attack },
				{ "defense", ball.status.defense },
				{ "mass", ball.status.mass },
				{ "radius", ball.status.radius },
				{ "restitution", ball.status.restitution },
				{ "friction", ball.status.friction },
				{ "pierce", ball.status.abilities.pierce },
				{ "split", ball.status.abilities.split },
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
					10));
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
	PublishState(game, true);

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

	ProcessPendingCommand(game);

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

	state["table"] = {
		{ "field_width", TableConfig::GetFieldWidth() },
		{ "field_depth", TableConfig::GetFieldDepth() },
		{ "walls", nlohmann::json::array() },
	};
	const std::vector<TableFrame*> tableFrames =
		game.GetObjects<TableFrame>();
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
		game.GetObjects<PlayerBall>();
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
	};
	if (player != nullptr)
	{
		state["player"]["position"] =
			VectorToJson(player->GetPosition());
		state["player"]["attack"] = player->GetAttack();
		state["player"]["defense"] = player->GetDefense();
		state["player"]["idle"] = player->IsIdle();
		if (player->GetBall() != nullptr)
		{
			state["player"]["radius"] =
				player->GetBall()->GetRadius();
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
			{ "owned", game.HasRelic(relic->type) },
		});
	}
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
			"applies_to_collision_pairs",
			nlohmann::json::array(
				{ "player_enemy", "enemy_enemy" })
		},
	};

	state["enemies"] = nlohmann::json::array();
	const std::vector<EnemyBall*> enemies =
		game.GetObjects<EnemyBall>();
	for (std::size_t enemyIndex = 0;
		enemyIndex < enemies.size();
		enemyIndex++)
	{
		EnemyBall* enemy = enemies[enemyIndex];
		if (enemy == nullptr)
		{
			continue;
		}
		state["enemies"].push_back({
			{ "target_id",
				"enemy:" + std::to_string(enemyIndex) },
			{ "enemy_id", enemy->GetEnemyId() },
			{ "hp", enemy->GetHP() },
			{ "max_hp", enemy->GetMaxHP() },
			{ "attack", enemy->GetAttack() },
			{ "defense", enemy->GetDefense() },
			{ "defeated", enemy->IsDefeated() },
			{ "stopped", enemy->IsStopped() },
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
			state["deck_balls"].push_back(
				BallDataToJson(*ball, index));
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
			? (game.m_DynamicBalanceLevel /
				game.m_DynamicBalanceLevelsPerAttackStep) *
				game.m_DynamicBalanceAttackStep
			: 0;
	state["dynamic_balance"] = {
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

	state["available_actions"] = nlohmann::json::array();
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
		state["available_actions"].push_back(
			"choose_destination");
	}
	else if (scene == "rest_site")
	{
		RestSiteScene* restSite =
			dynamic_cast<RestSiteScene*>(game.m_Scene);
		const bool actionUsed =
			restSite != nullptr && restSite->HasUsedAction();
		const bool canHeal =
			game.m_PlayerRunStatus.currentHp <
			game.m_PlayerRunStatus.maxHp;
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
		for (int index = 0; index < game.GetRelicCount(); index++)
		{
			const RelicDefinition* relic = game.GetRelic(index);
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
	}

	if (game.m_GameState == GameState::ClearReward)
	{
		if (game.m_IsClearRewardChosen)
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
	const nlohmann::json arguments =
		command.value(
			"arguments",
			nlohmann::json::object());
	const std::string scene = GetSceneName(game.m_Scene);

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
		game.StartNewRun();
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
		const std::string destination =
			arguments.value(
				"destination",
				std::string());
		if (destination == "rest")
		{
			game.ChangeScene(SceneType::RestSite);
			return CommandResult(true, "Moved to the rest site.");
		}
		if (destination == "shop")
		{
			game.ChangeScene(SceneType::Shop);
			return CommandResult(true, "Moved to the shop.");
		}
		if (destination == "battle")
		{
			game.StartNextBattle(
				ParseStageType(
					arguments.value(
						"stage_type",
						std::string("normal"))));
			return CommandResult(true, "Started the next battle.");
		}
		return CommandResult(
			false,
			"destination must be battle, rest, or shop.");
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
			game.GetObjects<PlayerBall>();
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
		return CommandResult(true, "Restored player HP.");
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
		if (!game.BuyRelic(relicIndex))
		{
			return CommandResult(
				false,
				"The requested relic could not be purchased. Check money.");
		}

		return CommandResult(
			true,
			std::string("Purchased relic: ") + relic->name + ".");
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
		game.StartNextBattle(
			ParseStageType(
				arguments.value(
					"stage_type",
					std::string("normal"))));
		return CommandResult(true, "Started the next battle.");
	}

	if (action == "choose_reward")
	{
		if (game.m_GameState != GameState::ClearReward ||
			game.m_IsClearRewardChosen)
		{
			return CommandResult(
				false,
				"A clear reward is not currently available.");
		}

		const std::string reward =
			arguments.value("reward", std::string());
		bool applied = false;
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
			applied = ballIndex >= 0 &&
				game.RestUpgradeBall(ballIndex);
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
		game.ChangeScene(SceneType::Select);
		game.m_GameState = GameState::AimingDirection;
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
