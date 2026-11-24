#include "GameRewardState.h"

#include <utility>

/// <summary>
/// クリア報酬の選択状態が有効かを返す。
/// </summary>
bool GameRewardState::IsActive() const
{
	return m_Active;
}

/// <summary>
/// クリア報酬が選択済みかを返す。
/// </summary>
bool GameRewardState::IsChosen() const
{
	return m_Chosen;
}

/// <summary>
/// 中ボス報酬のレリック選択が有効かを返す。
/// </summary>
bool GameRewardState::IsMidBossRelicSelectionActive() const
{
	return m_MidBossRelicSelectionActive;
}

/// <summary>
/// 現在選択している報酬の番号を返す。
/// </summary>
int GameRewardState::SelectedRewardIndex() const
{
	return m_SelectedRewardIndex;
}

/// <summary>
/// 報酬の対象として選択している球の番号を返す。
/// </summary>
int GameRewardState::SelectedRewardBallIndex() const
{
	return m_SelectedRewardBallIndex;
}

/// <summary>
/// 現在選択しているレリック候補の番号を返す。
/// </summary>
int GameRewardState::SelectedRelicOfferIndex() const
{
	return m_SelectedRelicOfferIndex;
}

/// <summary>
/// クリア報酬の表示メッセージを返す。
/// </summary>
const std::string& GameRewardState::Message() const
{
	return m_Message;
}

/// <summary>
/// 報酬として提示されている球の候補数を返す。
/// </summary>
int GameRewardState::BallOfferCount() const
{
	return static_cast<int>(m_BallOfferCatalogIndices.size());
}

/// <summary>
/// 球の候補番号をカタログ番号に変換し、範囲外なら無効値を返す。
/// </summary>
int GameRewardState::BallOfferCatalogIndex(int offerIndex) const
{
	return offerIndex >= 0 && offerIndex < BallOfferCount()
		? m_BallOfferCatalogIndices[static_cast<std::size_t>(offerIndex)]
		: -1;
}

/// <summary>
/// 報酬の候補となる球のカタログ番号を置き換える。
/// </summary>
void GameRewardState::SetBallOffers(std::vector<int> catalogIndices)
{
	m_BallOfferCatalogIndices = std::move(catalogIndices);
}

/// <summary>
/// 報酬の球候補一覧を空にする。
/// </summary>
void GameRewardState::ClearBallOffers()
{
	m_BallOfferCatalogIndices.clear();
}

/// <summary>
/// 新しいランに向けて、確認要求や球の入手状況、候補一覧を初期化する。
/// </summary>
void GameRewardState::ResetForRun()
{
	m_ConfirmationRequested = false;
	m_SkipRequested = false;
	m_ModalOpen = false;
	m_BallAcquired = false;
	m_MidBossRelicSelectionActive = false;
	m_BallOfferCatalogIndices.clear();
}

/// <summary>
/// 報酬の選択や確認状態を初期化し、指定のメッセージで画面の準備をする。
/// </summary>
void GameRewardState::Prepare(const std::string& message)
{
	m_SelectedRewardIndex = 0;
	m_SelectedRewardBallIndex = 0;
	m_Chosen = false;
	m_BallAcquired = false;
	m_ConfirmationRequested = false;
	m_SkipRequested = false;
	m_ModalOpen = false;
	m_Message = message;
	m_MidBossRelicSelectionActive = false;
}

/// <summary>
/// クリア報酬の選択状態を有効にする。
/// </summary>
void GameRewardState::Activate()
{
	m_Active = true;
}

/// <summary>
/// クリア報酬の選択状態を無効にする。
/// </summary>
void GameRewardState::Deactivate()
{
	m_Active = false;
}

/// <summary>
/// 報酬の選択を終了し、確認要求や球候補を解除する。
/// </summary>
void GameRewardState::Exit()
{
	m_Active = false;
	m_ConfirmationRequested = false;
	m_SkipRequested = false;
	m_ModalOpen = false;
	m_BallOfferCatalogIndices.clear();
}

/// <summary>
/// クリア報酬の表示メッセージを消去する。
/// </summary>
void GameRewardState::ClearMessage()
{
	m_Message.clear();
}

/// <summary>
/// クリア報酬の表示メッセージを設定する。
/// </summary>
void GameRewardState::SetMessage(std::string message)
{
	m_Message = std::move(message);
}

/// <summary>
/// クリア報酬を選択済みとして記録する。
/// </summary>
void GameRewardState::MarkChosen()
{
    m_Chosen = true;
}

/// <summary>
/// 報酬の選択内容を記録し、球を購入した場合は球の入手状態と次の選択位置を更新する。
/// </summary>
void GameRewardState::MarkChosen(
	int rewardIndex,
	int rewardBallIndex,
	std::string message)
{
	m_SelectedRewardIndex = rewardIndex;
	m_SelectedRewardBallIndex = rewardBallIndex;
	m_Message = std::move(message);
    // Buying a ball must preserve a bonus already received earlier.
    if (rewardIndex != 0) m_Chosen = true;
    if (rewardIndex == 0)
    {
        m_BallAcquired = true;
        m_SelectedRewardIndex = 2;
        m_SelectedRewardBallIndex = 0;
    }
}

/// <summary>
/// 中ボス報酬の選択位置を先頭に戻し、候補があるときに選択を有効にする。
/// </summary>
void GameRewardState::StartMidBossRelicSelection(bool hasOffers)
{
	m_SelectedRelicOfferIndex = 0;
	m_MidBossRelicSelectionActive = hasOffers;
}

/// <summary>
/// 中ボス報酬のレリック選択を終了する。
/// </summary>
void GameRewardState::FinishMidBossRelicSelection()
{
	m_MidBossRelicSelectionActive = false;
}

/// <summary>
/// レリック候補の選択を前に移動し、先頭では末尾に戻る。
/// </summary>
void GameRewardState::SelectPreviousRelic(int offerCount)
{
	if (offerCount > 0)
	{
		m_SelectedRelicOfferIndex =
			(m_SelectedRelicOfferIndex + offerCount - 1) % offerCount;
	}
}

/// <summary>
/// レリック候補の選択を次に移動し、末尾では先頭に戻る。
/// </summary>
void GameRewardState::SelectNextRelic(int offerCount)
{
	if (offerCount > 0)
	{
		m_SelectedRelicOfferIndex =
			(m_SelectedRelicOfferIndex + 1) % offerCount;
	}
}

/// <summary>
/// 報酬の選択を一つ前に移動し、対象となる球の選択位置を初期化する。
/// </summary>
void GameRewardState::SelectPreviousReward(int rewardCount)
{
	if (rewardCount > 0)
	{
		m_SelectedRewardIndex =
			(m_SelectedRewardIndex + rewardCount - 1) % rewardCount;
		m_SelectedRewardBallIndex = 0;
	}
}

/// <summary>
/// 報酬の選択を一つ次に進め、対象となる球の選択位置を初期化する。
/// </summary>
void GameRewardState::SelectNextReward(int rewardCount)
{
	if (rewardCount > 0)
	{
		m_SelectedRewardIndex =
			(m_SelectedRewardIndex + 1) % rewardCount;
		m_SelectedRewardBallIndex = 0;
	}
}

/// <summary>
/// 報酬の対象球の選択を前に移動し、先頭では末尾に戻る。
/// </summary>
void GameRewardState::SelectPreviousBall(int targetCount)
{
	if (targetCount > 0)
	{
		m_SelectedRewardBallIndex =
			(m_SelectedRewardBallIndex + targetCount - 1) % targetCount;
	}
}

/// <summary>
/// 報酬の対象球の選択を次に移動し、末尾では先頭に戻る。
/// </summary>
void GameRewardState::SelectNextBall(int targetCount)
{
	if (targetCount > 0)
	{
		m_SelectedRewardBallIndex =
			(m_SelectedRewardBallIndex + 1) % targetCount;
	}
}

/// <summary>
/// 画面で選択したレリック、報酬、対象球の各番号を状態に反映する。
/// </summary>
void GameRewardState::ApplyPresentationSelection(
	int relicOfferIndex,
	int rewardIndex,
	int rewardBallIndex)
{
	m_SelectedRelicOfferIndex = relicOfferIndex;
	m_SelectedRewardIndex = rewardIndex;
	m_SelectedRewardBallIndex = rewardBallIndex;
}

/// <summary>
/// クリア報酬の選択を確定する要求を記録する。
/// </summary>
void GameRewardState::RequestConfirmation()
{
	m_ConfirmationRequested = true;
}

/// <summary>
/// 報酬確認の要求があったかを返し、その要求を解除する。
/// </summary>
bool GameRewardState::ConsumeConfirmationRequest()
{
	return std::exchange(m_ConfirmationRequested, false);
}

/// <summary>
/// 報酬スキップの要求があったかを返し、その要求を解除する。
/// </summary>
bool GameRewardState::ConsumeSkipRequest()
{
	return std::exchange(m_SkipRequested, false);
}
