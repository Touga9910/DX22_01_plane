#include "BalanceAutoPlayer.h"
#include "BalanceAutoPlayerPolicy.h"
#include "Game.h"
#include "PlayerBallData.h"
#include "json/json.hpp"
#include <algorithm>
#include <limits>
#include <string>

#pragma execution_character_set("utf-8")

using namespace BalanceAutoPolicy;

// Balance Auto Rewardを適用
void BalanceAutoPlayer::ApplyReward(Game& game)
{
	bool hasMissingRelic = false;
	int nextRelicPrice = (std::numeric_limits<int>::max)();
	for (int index = 0; index < game.GetRelicCount(); index++)
	{
		const RelicDefinition* relic = game.GetRelic(index);
		if (relic != nullptr && !game.HasRelic(relic->type))
		{
			hasMissingRelic = true;
			nextRelicPrice = (std::min)(nextRelicPrice, relic->price);
		}
	}

	// 戦闘報酬の資金でレリックを買えるなら、球の過剰増加より先に資金を確保
	if (hasMissingRelic && game.GetPlayerMoney() < nextRelicPrice)
	{
		const int moneyBefore = game.GetPlayerMoney();
		game.GrantBalanceAutoMoney(kExtraRewardMoney);
		game.MarkBalanceAutoRewardChosen(
			2,
			0,
			"Auto Play: saved Money for a relic.");
		game.RecordBalanceEvent(
			"clear_reward_choice",
			{
				{ "controller", "autoplay" },
				{ "reward", "extra_money" },
				{ "reason", "save_for_relic" },
				{ "money_before", moneyBefore },
				{ "money_after", game.GetPlayerMoney() },
			});
		return;
	}

	const int missingOfferIndex = FindMissingClearRewardBallOffer(game);
	if (missingOfferIndex >= 0)
	{
		const int catalogIndex =
			game.GetClearRewardBallOfferCatalogIndex(missingOfferIndex);
		const PlayerBallData* offeredBall =
			game.GetClearRewardBallOffer(missingOfferIndex);
		const std::string definitionId = offeredBall != nullptr
			? offeredBall->definitionId
			: std::string();
		if (game.AddClearRewardBallOffer(missingOfferIndex))
		{
			game.NotifyBallAcquired(definitionId);
			game.MarkBalanceAutoRewardChosen(
				0,
				missingOfferIndex,
				"Auto Play: added an offered missing ball type.");
			game.RecordBalanceEvent(
				"clear_reward_choice",
				{
					{ "controller", "autoplay" },
					{ "reward", "new_ball" },
					{ "reason", "fill_missing_ball_type" },
					{ "offer_index", missingOfferIndex },
					{ "catalog_index", catalogIndex },
					{ "ball_id", definitionId },
				});
			return;
		}
	}

	const int upgradeTarget = FindUpgradeTarget(game);
	const PlayerBallData* ball =
		game.GetDeckBall(upgradeTarget);
	if (ball != nullptr)
	{
		const std::uint64_t instanceId = ball->instanceId;
		const std::string definitionId = ball->definitionId;
		const int upgradeLevelBefore = ball->upgradeLevel;
		const int upgradeCost =
			game.GetClearRewardUpgradeCost(upgradeTarget);
		const int moneyBefore = game.GetPlayerMoney();
		int chargedCost = 0;
		if (upgradeCost >= 0 &&
			game.GetPlayerMoney() >= upgradeCost &&
			game.ApplyClearRewardUpgrade(upgradeTarget, chargedCost))
		{
			game.MarkBalanceAutoRewardChosen(
				1,
				upgradeTarget,
				"Auto Play: paid Money and upgraded a primary ball.");
			game.RecordBalanceEvent(
				"clear_reward_choice",
				{
					{ "controller", "autoplay" },
					{ "reward", "upgrade_ball" },
					{ "reason", "concentrate_primary_ball" },
					{ "instance_id", instanceId },
					{ "ball_id", definitionId },
					{ "upgrade_level_before", upgradeLevelBefore },
					{ "upgrade_cost", chargedCost },
					{ "money_before", moneyBefore },
					{ "money_after", game.GetPlayerMoney() },
				});
			return;
		}

		if (upgradeCost >= 0 &&
			game.GetPlayerMoney() < upgradeCost)
		{
			game.GrantBalanceAutoMoney(kExtraRewardMoney);
			game.MarkBalanceAutoRewardChosen(
				2,
				0,
				"Auto Play: saved Money for a paid upgrade.");
			game.RecordBalanceEvent(
				"clear_reward_choice",
				{
					{ "controller", "autoplay" },
					{ "reward", "extra_money" },
					{ "reason", "save_for_upgrade" },
					{ "target_instance_id", instanceId },
					{ "target_upgrade_level", upgradeLevelBefore },
					{ "required_upgrade_cost", upgradeCost },
					{ "money_before", moneyBefore },
					{ "money_after", game.GetPlayerMoney() },
				});
			return;
		}
	}

	const int moneyBefore = game.GetPlayerMoney();
	game.GrantBalanceAutoMoney(kExtraRewardMoney);
	game.MarkBalanceAutoRewardChosen(
		2,
		0,
		"Auto Play: received extra Money.");
	game.RecordBalanceEvent(
		"clear_reward_choice",
		{
			{ "controller", "autoplay" },
			{ "reward", "extra_money" },
			{ "money_before", moneyBefore },
			{ "money_after", game.GetPlayerMoney() },
		});
}
