#include "BalanceAutoPlayer.h"
#include "BalanceAutoPlayerPolicy.h"
#include "Game.h"
#include "PlayerBallData.h"
#include "RunEventView.h"
#include "StageSelectScene.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>

#pragma execution_character_set("utf-8")

using namespace BalanceAutoPolicy;

/// <summary>
/// 保留中の球の調整や休憩・購入の必要性を考慮し、次のルートを自動で選ぶ。
/// </summary>
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
		// Try a resource/build adjustment before another ordinary battle when
		// there is enough HP or Money to make an informed event choice.
		if (game.GetPlayerMoney() >= 5 ||
			game.GetPlayerCurrentHp() * 100 >= game.GetPlayerMaxHp() * 60)
		{
			selectedRoute = findRoute("event");
		}
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

/// <summary>
/// 休憩所で回復が必要なら回復し、必要に応じて球の強化を選択する。
/// </summary>
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

/// <summary>
/// レリックや球の購入、不要な球の除外から、条件に合うショップ操作を実行する。
/// </summary>
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

/// <summary>
/// 支払い後のHPを確保し、球の価値や重複を調べてイベントを利用する。
/// 結果を受け取った後は通常の継続操作を通し、同じ効果を再適用しない。
/// </summary>
bool BalanceAutoPlayer::ApplyRunEventAction(Game& game)
{
	const auto event = game.CaptureRunEvent();
	if (event.result.pending) return game.ContinueRunEvent(event.result.repeatable);
	int bestChoice = -1;
	int bestTarget = -1;
	float bestScore = 0.0f;
	float averageValue = 0.0f;
	const int ballCount = game.GetDeckBallCount();
	for (int index = 0; index < ballCount; ++index)
		if (const auto* ball = game.GetDeckBall(index)) averageValue += GetAutoBallValue(*ball);
	if (ballCount > 0) averageValue /= static_cast<float>(ballCount);
	const auto copies = [&game, ballCount](const std::string& definitionId)
	{
		int count = 0;
		for (int index = 0; index < ballCount; ++index)
			if (const auto* ball = game.GetDeckBall(index); ball && ball->definitionId == definitionId) ++count;
		return count;
	};
	const int reserveHp = (std::max)(8, (event.maxHp * 45 + 99) / 100);
	for (int index = 0; index < static_cast<int>(event.choices.size()); ++index)
	{
		const auto& choice = event.choices[index];
		if (!choice.enabled) continue;
		if (choice.effectId == "leave")
		{
			if (bestChoice < 0) bestChoice = index;
			continue;
		}
		if (choice.hpCost > 0 && event.hp - choice.hpCost < reserveHp) continue;
		const float payment = choice.hpCost * 1.5f + choice.moneyCost * 0.5f;
		const auto consider = [&](float score, int target)
		{
			if (score > bestScore)
			{
				bestScore = score;
				bestChoice = index;
				bestTarget = target;
			}
		};
		if (choice.effectId == "heal")
		{
			consider(choice.amount * (IsHealNeeded(game) ? 4.0f : 2.0f) - payment, -1);
		}
		else if (choice.effectId == "money")
		{
			consider(static_cast<float>(choice.amount) - payment, -1);
		}
		else if (choice.effectId == "upgrade")
		{
			for (const auto& target : choice.targets)
			{
				const auto* ball = game.GetDeckBall(target.index);
				if (!ball || !ball->CanUpgrade()) continue;
				auto upgraded = *ball;
				upgraded.status = ball->upgradeTable[ball->upgradeLevel];
				++upgraded.upgradeLevel;
				consider(15.0f + GetAutoBallValue(upgraded) - GetAutoBallValue(*ball) - payment, target.index);
			}
		}
		else if (choice.effectId == "acquire" || choice.effectId == "trade")
		{
			const auto* offered = game.GetShopBall(choice.catalogIndex);
			if (!offered) continue;
			const float missingBonus = copies(offered->definitionId) == 0 ? 12.0f : 0.0f;
			if (choice.effectId == "acquire")
			{
				if (ballCount < kAutoMaximumDeckSize)
					consider(GetAutoBallValue(*offered) - averageValue + missingBonus - payment, -1);
			}
			else
			{
				for (const auto& target : choice.targets)
				{
					const auto* old = game.GetDeckBall(target.index);
					if (!old) continue;
					const float uniquePenalty = copies(old->definitionId) == 1 && old->definitionId != "player_standard" ? 16.0f : 0.0f;
					consider(GetAutoBallValue(*offered) - GetAutoBallValue(*old) + missingBonus - uniquePenalty - payment, target.index);
				}
			}
		}
		else if (choice.effectId == "remove" && ballCount > game.GetMinimumDeckSize())
		{
			for (const auto& target : choice.targets)
			{
				const auto* ball = game.GetDeckBall(target.index);
				if (!ball || (copies(ball->definitionId) <= 1 && ball->definitionId != "player_standard")) continue;
				const float gain = averageValue - GetAutoBallValue(*ball);
				if (gain > 0.0f) consider(gain + 8.0f - payment, target.index);
			}
		}
	}
	return bestChoice >= 0 && game.ChooseRunEvent(bestChoice, bestTarget);
}

