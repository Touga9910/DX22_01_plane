#include "BalanceAutoPlayer.h"
#include "BalanceAutoPlayerPolicy.h"
#include "Game.h"
#include "PlayerBallData.h"
#include "StageSelectScene.h"

#include <cstdint>
#include <iostream>
#include <string>

#pragma execution_character_set("utf-8")

using namespace BalanceAutoPolicy;

void BalanceAutoPlayer::ApplyRouteChoice(Game& game, StageSelectScene* stageSelect)
{
	PrunePendingBalls(game);
	int selectedRoute = -1;
	auto findRoute = [stageSelect](const char* routeId)
	{
		for (int index = 0;
			index < stageSelect->GetRouteNodeCount();
			index++)
		{
			if (std::string(stageSelect->GetRouteIdAt(index)) == routeId)
			{
				return index;
			}
		}
		return -1;
	};

	if (game.IsFinalBossRoute())
	{
		selectedRoute = 0;
	}
	else if (IsHealNeeded(game))
	{
		selectedRoute = findRoute("rest");
	}
	if (selectedRoute < 0 && HasShopAction(game))
	{
		selectedRoute = findRoute("shop");
	}
	if (selectedRoute < 0 &&
		FindUpgradeTarget(game) >= 0)
	{
		selectedRoute = findRoute("rest");
	}
	if (selectedRoute < 0)
	{
		selectedRoute = findRoute("battle");
	}
	if (selectedRoute < 0)
	{
		selectedRoute = findRoute("midboss");
	}
	if (selectedRoute < 0)
	{
		selectedRoute = 0;
	}
	stageSelect->ChooseRoute(selectedRoute, "autoplay");
}

void BalanceAutoPlayer::ApplyRestSiteAction(Game& game)
{
	if (IsHealNeeded(game) && game.RestHeal())
	{
		std::cout
			<< "[BalanceAutoPlay] RestSite: recovered "
			<< game.GetRestHealPercent()
			<< "% of max HP"
			<< std::endl;
	}
	else
	{
		const int ballIndex =
			FindUpgradeTarget(game);
		const PlayerBallData* ball =
			game.GetDeckBall(ballIndex);
		if (ball != nullptr)
		{
			const std::uint64_t instanceId = ball->instanceId;
			const std::string definitionId = ball->definitionId;
			if (game.RestUpgradeBall(ballIndex))
			{
				std::cout
					<< "[BalanceAutoPlay] RestSite: upgraded "
					<< definitionId
					<< " (instance " << instanceId << ")"
					<< std::endl;
			}
		}
	}

	game.LeaveRestSite();
}

void BalanceAutoPlayer::ApplyShopAction(Game& game)
{
	const int relicIndex = FindRelicToBuy(game);
	if (relicIndex >= 0 && game.BuyShopRelic(relicIndex))
	{
		const RelicDefinition* relic = game.GetRelic(relicIndex);
		std::cout
			<< "[BalanceAutoPlay] Shop: purchased relic "
			<< (relic != nullptr ? relic->name : "unknown")
			<< std::endl;
	}
	else
	{
		const int ballIndex = FindWeakestBall(game);
		const PlayerBallData* ball =
			game.GetDeckBall(ballIndex);
		if (ball != nullptr &&
			game.GetPlayerMoney() >= kAutoShopRemoveCost &&
			game.GetDeckBallCount() >
				game.GetMinimumDeckSize())
		{
			const std::uint64_t instanceId = ball->instanceId;
			const std::string definitionId = ball->definitionId;
			if (game.RemoveShopBall(ballIndex, kAutoShopRemoveCost))
			{
				std::cout
					<< "[BalanceAutoPlay] Shop: removed "
					<< definitionId
					<< " (instance " << instanceId << ")"
					<< std::endl;
			}
		}
		else
		{
			const int offerIndex =
				FindMissingCatalogBall(game);
			if (offerIndex >= 0 &&
				game.BuyShopBallOffer(offerIndex, kAutoShopBallCost))
			{
				std::cout
					<< "[BalanceAutoPlay] Shop: purchased ball"
					<< std::endl;
			}
		}
	}

	game.LeaveShop();
}

