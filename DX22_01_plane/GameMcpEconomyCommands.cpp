#include "GameMcpCommandHandlers.h"
#include "GameMcpCommands.h"
#include "GameMcpJson.h"
#include "Game.h"
#include "GameTypes.h"
#include "PlayerBallData.h"

#include <cstdint>
#include <string>

std::optional<nlohmann::json> GameMcpCommandHandlers::TryEconomy(
	Game& game,
	const std::string& action,
	const nlohmann::json& arguments,
	const std::string& scene)
{
	using namespace GameMcpJson;

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

	if (action == "buy_ball")
	{
		if (scene != "shop")
		{
			return CommandResult(
				false,
				"Balls can only be purchased at the shop.");
		}
		const int offerIndex = arguments.value("offer_index", -1);
		if (offerIndex < 0 ||
			offerIndex >= game.GetShopBallOfferCount() ||
			!game.BuyShopBallOffer(offerIndex, kShopBallCost))
		{
			return CommandResult(
				false,
				"The offered ball could not be purchased. Check the offer and Money.");
		}
		recordBuildDecision();
		return CommandResult(true, "Purchased the requested shop ball.");
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

	if (action == "choose_reward")
	{
		if (!game.IsClearRewardActive() ||
			game.IsMidBossRelicSelectionActive())
		{
			return CommandResult(
				false,
				"A clear reward is not currently available.");
		}

		const int moneyBefore = game.GetPlayerMoney();
		const std::string reward =
			arguments.value("reward", std::string());
		if (game.IsClearRewardChosen() && reward != "new_ball")
			return CommandResult(false, "The bonus reward has already been received.");
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

	return std::nullopt;
}
