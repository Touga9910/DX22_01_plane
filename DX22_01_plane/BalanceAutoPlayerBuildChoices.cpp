#include "BalanceAutoPlayer.h"
#include "BalanceAutoPlayerPolicy.h"
#include "Game.h"
#include "PlayerBallData.h"
#include <algorithm>
#include <array>
#include <limits>
#include <string>

#pragma execution_character_set("utf-8")

using namespace BalanceAutoPolicy;

namespace BalanceAutoPolicy
{
	float GetAutoBallValue(const PlayerBallData& ball)
	{
		float value =
			static_cast<float>(ball.status.attack) * 4.0f +
			static_cast<float>(ball.upgradeLevel) * 5.0f;
		if (ball.status.abilities.pierce)
		{
			value += 6.0f;
		}
		if (ball.status.abilities.anchor)
		{
			value += 5.0f;
		}
		if (ball.category == BallCategory::Bounce)
		{
			value += 4.0f;
		}
		return value;
	}
}

// Balance Auto Heal Neededかどうかを判定
bool BalanceAutoPlayer::IsHealNeeded(const Game& game) const
{
	if (game.GetPlayerMaxHp() <= 0 ||
		game.GetPlayerCurrentHp() >= game.GetPlayerMaxHp())
	{
		return false;
	}

	const int thresholdPercent = game.IsBossPreparation() ? 75 : 60;
	return game.GetPlayerCurrentHp() * 100 <=
		game.GetPlayerMaxHp() * thresholdPercent;
}

// Balance Auto Relic To Buyを検索
int BalanceAutoPlayer::FindRelicToBuy(const Game& game) const
{
	const auto isCandidate = [this, &game](int index)
	{
		return game.GetCurrentSceneType() != SceneType::Shop ||
			game.GetShopRelicOfferCount() == 0 || game.IsShopRelicOffered(index);
	};
	const bool needsRecovery =
		game.GetPlayerCurrentHp() * 2 <= game.GetPlayerMaxHp();
	const std::array<RelicType, 4> priority = needsRecovery
		? std::array<RelicType, 4>{
			RelicType::EmergencyRepairKit,
			RelicType::AllBallAttackUp,
			RelicType::CollisionAttackUp,
			RelicType::BankShot }
		: std::array<RelicType, 4>{
			RelicType::AllBallAttackUp,
			RelicType::CollisionAttackUp,
			RelicType::EmergencyRepairKit,
			RelicType::BankShot };

	for (RelicType type : priority)
	{
		for (int index = 0; index < game.GetRelicCount(); index++)
		{
			const RelicDefinition* relic = game.GetRelic(index);
			if (relic != nullptr &&
				relic->type == type &&
				isCandidate(index) &&
				!game.HasRelic(type) &&
				game.GetPlayerMoney() >= relic->price)
			{
				return index;
			}
		}
	}
	for (int index = 0; index < game.GetRelicCount(); index++)
	{
		const RelicDefinition* relic = game.GetRelic(index);
		if (relic != nullptr && isCandidate(index) &&
			!game.HasRelic(relic->type) &&
			game.GetPlayerMoney() >= relic->price)
		{
			return index;
		}
	}

	return -1;
}

// Balance Auto Weakest Ballを検索
int BalanceAutoPlayer::FindWeakestBall(const Game& game) const
{
	const int ballCount = game.GetDeckBallCount();
	if (ballCount <= game.GetMinimumDeckSize() ||
		game.GetPlayerMoney() < kAutoShopRemoveCost)
	{
		return -1;
	}

	int weakestIndex = -1;
	float weakestScore = (std::numeric_limits<float>::max)();
	for (int index = 0; index < ballCount; index++)
	{
		const PlayerBallData* ball = game.GetDeckBall(index);
		if (ball == nullptr)
		{
			continue;
		}

		int sameTypeCount = 0;
		for (int otherIndex = 0; otherIndex < ballCount; otherIndex++)
		{
			const PlayerBallData* other =
				game.GetDeckBall(otherIndex);
			if (other != nullptr &&
				other->definitionId == ball->definitionId)
			{
				sameTypeCount++;
			}
		}

		// タイプの種類を減らさないよう、重複球から削除
		const float score = GetAutoBallValue(*ball) +
			(sameTypeCount <= 1 ? 1000.0f : 0.0f);
		if (score < weakestScore)
		{
			weakestScore = score;
			weakestIndex = index;
		}
	}

	return weakestIndex;
}

// Balance Auto Shop提示内の未所持Ballを検索
int BalanceAutoPlayer::FindMissingCatalogBall(const Game& game) const
{
	if (game.GetDeckBallCount() >= kAutoMaximumDeckSize)
	{
		return -1;
	}

	const std::array<const char*, 10> priority =
	{
		"player_bounce",
		"player_cushion_charge",
		"player_ricochet_finisher",
		"player_anchor",
		"player_anchor_finisher",
		"player_pierce",
		"player_minion_finisher",
		"player_pierce_finisher",
		"player_heavy",
		"player_standard",
	};
	for (const char* definitionId : priority)
	{
		bool alreadyOwned = false;
		for (int index = 0;
			index < game.GetDeckBallCount();
			index++)
		{
			const PlayerBallData* ball =
				game.GetDeckBall(index);
			if (ball != nullptr && ball->definitionId == definitionId)
			{
				alreadyOwned = true;
				break;
			}
		}
		if (alreadyOwned)
		{
			continue;
		}

		for (int offerIndex = 0;
			offerIndex < game.GetShopBallOfferCount();
			offerIndex++)
		{
			const PlayerBallData* offeredBall =
				game.GetShopBallOffer(offerIndex);
			if (offeredBall != nullptr &&
				offeredBall->definitionId == definitionId)
			{
				return offerIndex;
			}
		}
	}

	return -1;
}

// Balance Auto Clear Reward内の未所持ボール提示を検索
int BalanceAutoPlayer::FindMissingClearRewardBallOffer(const Game& game) const
{
	if (game.GetDeckBallCount() >=
		kAutoMaximumDeckSize)
	{
		return -1;
	}

	const std::array<const char*, 10> priority =
	{
		"player_bounce",
		"player_cushion_charge",
		"player_ricochet_finisher",
		"player_anchor",
		"player_anchor_finisher",
		"player_pierce",
		"player_minion_finisher",
		"player_pierce_finisher",
		"player_heavy",
		"player_standard",
	};
	for (const char* definitionId : priority)
	{
		bool alreadyOwned = false;
		for (int index = 0;
			index < game.GetDeckBallCount();
			index++)
		{
			const PlayerBallData* ball =
				game.GetDeckBall(index);
			if (ball != nullptr && ball->definitionId == definitionId)
			{
				alreadyOwned = true;
				break;
			}
		}
		if (alreadyOwned)
		{
			continue;
		}

		for (int offerIndex = 0;
			offerIndex < game.GetClearRewardBallOfferCount();
			offerIndex++)
		{
			const PlayerBallData* offeredBall =
				game.GetClearRewardBallOffer(offerIndex);
			if (offeredBall != nullptr &&
				offeredBall->definitionId == definitionId)
			{
				return offerIndex;
			}
		}
	}

	return -1;
}

// Balance Auto Upgrade Targetを検索
int BalanceAutoPlayer::FindUpgradeTarget(const Game& game) const
{
	int bestIndex = -1;
	float bestScore = -(std::numeric_limits<float>::max)();
	for (int index = 0;
		index < game.GetDeckBallCount();
		index++)
	{
		const PlayerBallData* ball =
			game.GetDeckBall(index);
		if (ball == nullptr || !ball->CanUpgrade())
		{
			continue;
		}

		const float score = GetAutoBallValue(*ball) +
			static_cast<float>(ball->upgradeLevel) * 25.0f;
		if (score > bestScore)
		{
			bestScore = score;
			bestIndex = index;
		}
	}

	return bestIndex;
}

// Balance Auto Shop Actionを保持しているか判定
bool BalanceAutoPlayer::HasShopAction(const Game& game) const
{
	return FindRelicToBuy(game) >= 0 ||
		FindWeakestBall(game) >= 0 ||
		(game.GetPlayerMoney() >= kAutoShopBallCost &&
			FindMissingCatalogBall(game) >= 0);
}
