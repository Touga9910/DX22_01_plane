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

/// <summary>
/// バランス検証用の自動プレイが有効かを返す。
/// </summary>
bool Game::IsBalanceAutoPlayEnabled() const
{
	return m_BalanceRuntime->autoPlayer.IsEnabled();
}

/// <summary>
/// 自動プレイが選んだ球候補を選択状態に反映し、プレビューを更新する。
/// </summary>
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

/// <summary>
/// デバッグエディターが開いているかを返す。
/// </summary>
bool Game::IsDebugEditorOpen() const
{
	return m_DebugController->IsEditorOpen();
}

/// <summary>
/// 指定した球種でショットした回数を集計から取得する。
/// </summary>
int Game::GetBallShotCount(const std::string& definitionId) const
{
	const auto& shotCounts =
		m_PersistenceRuntime->Statistics().GetState().ballShotCounts;
	const auto found = shotCounts.find(definitionId);
	return found != shotCounts.end() ? found->second : 0;
}

/// <summary>
/// 自動プレイ中のクリア報酬画面で、有効な金額を所持金に加える。
/// </summary>
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

/// <summary>
/// 指定した球種を獲得したことをイベントとして通知する。
/// </summary>
void Game::NotifyBallAcquired(const std::string& definitionId)
{
	PublishGameEvent(BallAcquiredEvent{ definitionId });
}

/// <summary>
/// 自動プレイで確定した報酬の選択内容と表示メッセージを記録する。
/// </summary>
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

/// <summary>
/// 指定した球が自動プレイの調整対象かを返す。
/// </summary>
bool Game::IsBallAdjustmentCandidate(
	std::uint64_t instanceId) const
{
	return m_BalanceRuntime->autoPlayer.IsBallAdjustmentCandidate(instanceId);
}

/// <summary>
/// 自動プレイで利用できる休憩所の効果があるかを返す。
/// </summary>
bool Game::HasAvailableRestBenefit() const
{
	return m_BalanceRuntime->autoPlayer.HasAvailableRestBenefit(*this);
}

/// <summary>
/// 自動プレイで処理を保留している球を一覧から除く。
/// </summary>
void Game::RemoveBalanceAutoPendingBall(
	std::uint64_t instanceId)
{
	m_BalanceRuntime->autoPlayer.RemovePendingBall(instanceId);
}

/// <summary>
/// 自動プレイで保留中の球をゲームの状態に照らして整理する。
/// </summary>
void Game::PruneBalanceAutoPendingBalls()
{
	m_BalanceRuntime->autoPlayer.PrunePendingBalls(*this);
}

/// <summary>
/// 指定したコンポーネントが現在のワールドに存在するかを返す。
/// </summary>
bool Game::ContainsComponent(const Component* component) const
{
	return GameRuntime::World().Contains(component);
}
