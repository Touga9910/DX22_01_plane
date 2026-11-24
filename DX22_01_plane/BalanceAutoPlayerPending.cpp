#include "BalanceAutoPlayer.h"
#include "Game.h"
#include "PlayerBallData.h"
#include <algorithm>
#include <iostream>
#include <string>

#pragma execution_character_set("utf-8")

/// <summary>
/// 調整待ちの球から、現在のデッキにある強化可能な球の位置を返す。
/// </summary>
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

/// <summary>
/// 調整待ちの球から、現在のデッキにある強化できない球の除外候補を探す。
/// </summary>
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

/// <summary>
/// 指定した球のインスタンスIDが調整待ちとして登録済みかを返す。
/// </summary>
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

/// <summary>
/// 満タンの敵を倒し切れなかったショットの球を、調整待ちの一覧に登録する。
/// </summary>
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

/// <summary>
/// 休憩所で回復または球の強化を行えるかを返す。
/// </summary>
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

/// <summary>
/// 指定した球を調整待ちの一覧から取り除く。
/// </summary>
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

/// <summary>
/// デッキからなくなった球を調整待ちの一覧から取り除く。
/// </summary>
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
