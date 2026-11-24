#include "GameMcpStateSerialization.h"
#include "GameMcpJson.h"
#include "Game.h"
#include "GameMcpState.h"
#include "json/json.hpp"
#include "PlayerBallData.h"

using namespace GameMcpJson;

namespace GameMcpStateSerialization
{
/// <summary>
/// レリックの一覧と所持状況、関連する効果条件をJSONに追加する。
/// </summary>
void AppendRelics(Game& game, const GameMcpReadSnapshot& snapshot, nlohmann::json& state)
{
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
}

/// <summary>
/// 球の所持状況、入手候補、強化や削除の選択肢をJSONに追加する。
/// </summary>
void AppendInventory(Game& game, const GameMcpReadSnapshot& snapshot, nlohmann::json& state)
{
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
		offer["price"] = kShopBallCost;
		offer["affordable"] = game.GetPlayerMoney() >= kShopBallCost;
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
		offer["price"] = game.GetClearRewardBallCost();
        offer["affordable"] = game.GetPlayerMoney() >= game.GetClearRewardBallCost() && !game.IsClearRewardBallAcquired();
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
}

}
