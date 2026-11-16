#include "BalanceAutoPlayer.h"
#include "Game.h"
#include "PlayerBallData.h"
#include <algorithm>
#include <iostream>
#include <string>

#pragma execution_character_set("utf-8")

// Balance Auto Pending Upgradeable Ballを検索
int BalanceAutoPlayer::FindPendingUpgradeableBall(const Game& game) const
{
	for (const std::uint64_t instanceId :
		m_PendingBallAdjustments)
	{
		for (int index = 0;
			index < game.GetDeckBallCount();
			index++)
		{
			const PlayerBallData* ball =
				game.GetDeckBall(index);
			if (ball != nullptr &&
				ball->instanceId == instanceId &&
				ball->CanUpgrade())
			{
				return index;
			}
		}
	}

	return -1;
}

// Balance Auto Pending Removal Ballを検索
int BalanceAutoPlayer::FindPendingRemovalBall(const Game& game) const
{
	for (const std::uint64_t instanceId :
		m_PendingBallAdjustments)
	{
		for (int index = 0;
			index < game.GetDeckBallCount();
			index++)
		{
			const PlayerBallData* ball =
				game.GetDeckBall(index);
			if (ball != nullptr &&
				ball->instanceId == instanceId &&
				!ball->CanUpgrade())
			{
				return index;
			}
		}
	}

	return -1;
}

// Ball Adjustment Candidateかどうかを判定
bool BalanceAutoPlayer::IsBallAdjustmentCandidate(
	std::uint64_t instanceId) const
{
	return instanceId != 0 &&
		std::find(
			m_PendingBallAdjustments.begin(),
			m_PendingBallAdjustments.end(),
			instanceId) !=
		m_PendingBallAdjustments.end();
}

void BalanceAutoPlayer::NotifyFullHpEnemySurvived(
	const PlayerBallData* currentBall)
{
	if (currentBall == nullptr ||
		currentBall->instanceId == 0 ||
		IsBallAdjustmentCandidate(currentBall->instanceId))
	{
		return;
	}

	m_PendingBallAdjustments.push_back(currentBall->instanceId);
	std::cout
		<< "[BalanceAutoPlay] Ball adjustment pending: "
		<< currentBall->definitionId
		<< " (instance " << currentBall->instanceId << ")"
		<< std::endl;
}

// Available Rest Benefitを保持しているか判定
bool BalanceAutoPlayer::HasAvailableRestBenefit(const Game& game) const
{
	if (game.CanRestHeal())
	{
		return true;
	}

	const int ballCount =
		game.GetDeckBallCount();
	for (int index = 0; index < ballCount; index++)
	{
		const PlayerBallData* ball =
			game.GetDeckBall(index);
		if (ball != nullptr && ball->CanUpgrade())
		{
			return true;
		}
	}

	return false;
}

// Balance Auto Pending Ballを取り除く。
void BalanceAutoPlayer::RemovePendingBall(
	std::uint64_t instanceId)
{
	m_PendingBallAdjustments.erase(
		std::remove(
			m_PendingBallAdjustments.begin(),
			m_PendingBallAdjustments.end(),
			instanceId),
		m_PendingBallAdjustments.end());
}

// Prune Balance Auto Pending Balls の処理を実行
void BalanceAutoPlayer::PrunePendingBalls(const Game& game)
{
	m_PendingBallAdjustments.erase(
		std::remove_if(
			m_PendingBallAdjustments.begin(),
			m_PendingBallAdjustments.end(),
			[this, &game](std::uint64_t instanceId)
			{
				for (int index = 0;
					index < game.GetDeckBallCount();
					index++)
				{
					const PlayerBallData* ball =
						game.GetDeckBall(index);
					if (ball != nullptr &&
						ball->instanceId == instanceId)
					{
						return false;
					}
				}
				return true;
			}),
		m_PendingBallAdjustments.end());
}
