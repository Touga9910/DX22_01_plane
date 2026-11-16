#include "GameMcpStateSerialization.h"
#include "GameMcpJson.h"
#include "Game.h"
#include "GameMcpState.h"
#include "json/json.hpp"
#include "StageSelectScene.h"
#include "RestSiteScene.h"
#include "TitleScene.h"
#include "PlayerBall.h"
#include "PlayerBallData.h"
#include "RunMap.h"

using namespace GameMcpJson;

namespace GameMcpStateSerialization
{
void AppendAvailableActions(Game& game, const GameMcpReadSnapshot& snapshot, const PlayerBall* player, nlohmann::json& state)
{
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
		if (game.GetShopBallOfferCount() > 0 &&
			game.GetPlayerMoney() >= kShopBallCost)
		{
			state["available_actions"].push_back("buy_ball");
		}
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
            { "optional_new_ball_then_one_bonus", true },
            { "new_ball_price", game.GetClearRewardBallCost() },
            { "new_ball_acquired", game.IsClearRewardBallAcquired() },
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
}

void RestrictDebugActions(Game& game, const GameMcpReadSnapshot& snapshot, nlohmann::json& state)
{
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
}

}
