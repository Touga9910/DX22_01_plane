#include "GameMcpStateSerialization.h"
#include "GameMcpJson.h"
#include "Game.h"
#include "GameMcpState.h"
#include "json/json.hpp"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "GameObject.h"
#include "TableFrame.h"
#include "TableConfig.h"
#include "Pocket.h"
#include "PlayerBall.h"
#include "PlayerBallData.h"
#include "BallComponent.h"
#include "StageData.h"

using namespace GameMcpJson;

namespace GameMcpStateSerialization
{
namespace
{
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
				{ "can_charge", spawn.enemyData.canCharge },
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
			{ "combat_mode", ToString(stage.combatMode) },
			{ "difficulty", stage.difficulty },
			{ "par", stage.par },
			{ "enemies", std::move(enemies) },
			{ "break_balls", std::move(breakBalls) },
		};
	}
}

void AppendTable(Game& game, const GameMcpReadSnapshot& snapshot, const std::vector<std::string>& stageEnemyIds, nlohmann::json& state)
{
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
			{ "instant_defeat", snapshot.pocketRules.enemyInstantDefeat },
			{ "high_hp_result", "instant_defeat" },
			{ "returns_per_turn", 0 },
			{ "return_position", nullptr },
			{ "current_stage_type",
				StageTypeToMcpString(snapshot.pocketRules.stageType) },
			{ "current_finisher_hp_ratio", 1.0f },
			{ "return_queue_size", 0 },
		} },
	};

	state["stage_layout_control"] = {
		{ "application_timing", "next_battle_spawn" },
		{ "maximum_enemy_count", kMaximumMcpStageEnemyCount },
		{ "allowed_enemy_ids", stageEnemyIds },
		{ "queued_override",
			snapshot.queuedStageOverride.has_value()
				? StageDataToJson(snapshot.queuedStageOverride.value())
				: nlohmann::json(nullptr) },
		{ "current_override",
			snapshot.currentStageOverride.has_value()
				? StageDataToJson(snapshot.currentStageOverride.value())
				: nlohmann::json(nullptr) },
	};
}

const PlayerBall* AppendPlayer(Game& game, const GameMcpReadSnapshot& snapshot, nlohmann::json& state)
{
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
	return player;
}

void AppendSelectedBall(Game& game, const GameMcpReadSnapshot& snapshot, const PlayerBall* player, nlohmann::json& state)
{
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
}

}
