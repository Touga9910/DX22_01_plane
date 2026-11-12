#include "Game.h"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "GameRewardState.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "RunController.h"
#include "EnemyBall.h"
#include "UiText.h"
#include "json/json.hpp"

#include <algorithm>
#include <cstdio>

namespace
{
    constexpr int kClearRewardFirstUpgradeCost = 15;
    constexpr int kClearRewardSecondUpgradeCost = 30;
}

int Game::GetClearRewardUpgradeCost(int ballIndex) const
{
	const PlayerBallData* ball =
		m_RunController->Deck().GetRewardTarget(ballIndex);
	if (ball == nullptr || !ball->CanUpgrade())
	{
		return -1;
	}

	if (ball->upgradeLevel == 0)
	{
		return kClearRewardFirstUpgradeCost;
	}
	if (ball->upgradeLevel == 1)
	{
		return kClearRewardSecondUpgradeCost;
	}
	return -1;
}

// Clear Reward Upgradeを適用する。

bool Game::ApplyClearRewardUpgrade(
	int ballIndex,
	int& chargedCost)
{
	chargedCost = 0;
	if (!IsClearRewardActive())
	{
		return false;
	}

	const int cost = GetClearRewardUpgradeCost(ballIndex);
	if (cost < 0 || m_RunController->Status().money < cost)
	{
		return false;
	}

	if (!RestUpgradeBall(ballIndex))
	{
		return false;
	}

	if (!m_RunController->SpendMoney(cost))
	{
		return false;
	}
	chargedCost = cost;
	return true;
}

// Rest Upgrade Ball の処理を実行する。

int Game::CalculateStageRewardMoney() const
{
	constexpr int BASE_CLEAR_MONEY = 5;
	int totalRewardMoney = BASE_CLEAR_MONEY;

	std::vector<EnemyBall*> enemies =
		GameRuntime::World().GetComponents<EnemyBall>();

	for (EnemyBall* enemy : enemies)
	{
		if (enemy == nullptr)
		{
			continue;
		}

		// 倒した敵の報酬だけを取得する
		if (!enemy->IsDefeated())
		{
			continue;
		}

		totalRewardMoney +=
			(std::max)(0, enemy->GetRewardMoney());
	}

	return totalRewardMoney;
}

// Collect Stage Reward Money の処理を実行する。

void Game::CollectStageRewardMoney()
{
	// StartClearRewardが複数回呼ばれても二重取得しない
	if (m_RunController->IsStageRewardCollected())
	{
		return;
	}

	const int moneyBefore = m_RunController->Status().money;
	if (!m_RunController->CollectStageReward(
		CalculateStageRewardMoney()))
	{
		return;
	}
	const int rewardMoney =
		m_RunController->GetCurrentStageRewardMoney();

	char rewardMessage[128]{};
	sprintf_s(
		rewardMessage,
		UiText::RewardMoneyFormat,
		rewardMoney);
	m_RewardState->SetMessage(rewardMessage);
	RecordBalanceEvent(
		"stage_money_reward",
		{
			{ "amount", rewardMoney },
			{ "money_before", moneyBefore },
			{ "money_after", m_RunController->Status().money },
		});
}

