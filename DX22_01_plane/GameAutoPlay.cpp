#include "Game.h"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "GameBalanceRuntime.h"
#include "GamePersistenceRuntime.h"
#include "GameRewardState.h"
#include "GameBallSelectionState.h"
#include "GameDebugController.h"
#include "RunController.h"
#include <string>

#pragma execution_character_set("utf-8")

bool Game::IsBalanceAutoPlayEnabled() const
{
	return m_BalanceRuntime->autoPlayer.IsEnabled();
}

void Game::ApplyBalanceAutoBallSelection(int offerIndex)
{
	if (!m_BallSelectionState->SelectOffer(
		offerIndex,
		GetBallOfferCount()))
	{
		return;
	}
	ApplySelectedBallPreview();
}

bool Game::IsDebugEditorOpen() const
{
	return m_DebugController->IsEditorOpen();
}

int Game::GetBallShotCount(const std::string& definitionId) const
{
	const auto& shotCounts =
		m_PersistenceRuntime->Statistics().GetState().ballShotCounts;
	const auto found = shotCounts.find(definitionId);
	return found != shotCounts.end() ? found->second : 0;
}

bool Game::GrantBalanceAutoMoney(int amount)
{
	if (!m_BalanceRuntime->autoPlayer.IsEnabled() ||
		!IsClearRewardActive() ||
		amount <= 0)
	{
		return false;
	}
	m_RunController->AddMoney(amount);
	return true;
}

void Game::NotifyBallAcquired(const std::string& definitionId)
{
	PublishGameEvent(BallAcquiredEvent{ definitionId });
}

void Game::MarkBalanceAutoRewardChosen(
	int rewardIndex,
	int rewardBallIndex,
	const std::string& message)
{
	m_RewardState->MarkChosen(
		rewardIndex,
		rewardBallIndex,
		message);
}

bool Game::IsBallAdjustmentCandidate(
	std::uint64_t instanceId) const
{
	return m_BalanceRuntime->autoPlayer.IsBallAdjustmentCandidate(instanceId);
}

bool Game::HasAvailableRestBenefit() const
{
	return m_BalanceRuntime->autoPlayer.HasAvailableRestBenefit(*this);
}

void Game::RemoveBalanceAutoPendingBall(
	std::uint64_t instanceId)
{
	m_BalanceRuntime->autoPlayer.RemovePendingBall(instanceId);
}

void Game::PruneBalanceAutoPendingBalls()
{
	m_BalanceRuntime->autoPlayer.PrunePendingBalls(*this);
}

// 指定したComponentが現在のGameWorldに属しているかを返す。
bool Game::ContainsComponent(const Component* component) const
{
	return GameRuntime::World().Contains(component);
}
