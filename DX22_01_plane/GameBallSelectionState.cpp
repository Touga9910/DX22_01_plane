#include "GameBallSelectionState.h"

#include <algorithm>

namespace
{
	constexpr float kHandTransitionSeconds = 0.24f;
}

/// <summary>
/// 現在選択している球候補の位置を返す。
/// </summary>
int GameBallSelectionState::SelectedOfferIndex() const
{
	return m_SelectedOfferIndex;
}

/// <summary>
/// 球候補の保持機能廃止後の互換値としてマイナス1を返す。
/// </summary>
int GameBallSelectionState::HeldOfferIndex() const
{
	return -1; // Legacy read API; holding is removed.
}

/// <summary>
/// 手札を表示し、先頭の球候補を選択した状態から球選択を始める。
/// </summary>
void GameBallSelectionState::Begin(int /*offerCount*/, int /*legacyHeldOfferIndex*/)
{
	m_FlowState = FlowState::Selecting;
	m_HandHiddenRatio = 0.0f;
	m_SelectedOfferIndex = 0;
}

/// <summary>
/// 経過時間に応じて手札の表示を切り替え、遷移完了時に選択状態を進める。
/// </summary>
void GameBallSelectionState::Update(float deltaSeconds)
{
	const float step = (std::max)(0.0f, deltaSeconds) /
		kHandTransitionSeconds;
	if (m_FlowState == FlowState::Hiding)
	{
		m_HandHiddenRatio = (std::min)(1.0f, m_HandHiddenRatio + step);
		if (m_HandHiddenRatio >= 1.0f)
		{
			m_FlowState = FlowState::ShotReady;
		}
	}
	else if (m_FlowState == FlowState::Showing)
	{
		m_HandHiddenRatio = (std::max)(0.0f, m_HandHiddenRatio - step);
		if (m_HandHiddenRatio <= 0.0f)
		{
			m_FlowState = FlowState::Selecting;
		}
	}
}

/// <summary>
/// 選択位置が候補の範囲内なら現在の球候補として記録する。
/// </summary>
bool GameBallSelectionState::SelectOffer(int offerIndex, int offerCount)
{
	if (offerIndex < 0 || offerIndex >= offerCount)
	{
		return false;
	}
	m_SelectedOfferIndex = offerIndex;
	return true;
}

/// <summary>
/// 操作可能な状態で有効な候補を選んでいれば、手札を隠す遷移を開始する。
/// </summary>
bool GameBallSelectionState::ConfirmSelection(int offerCount)
{
	if (!IsSelectionInteractive() ||
		m_SelectedOfferIndex < 0 || m_SelectedOfferIndex >= offerCount)
	{
		return false;
	}
	m_FlowState = FlowState::Hiding;
	m_HandHiddenRatio = 0.0f;
	return true;
}

/// <summary>
/// ショット準備中なら準備を取り消し、手札を再表示する遷移を開始する。
/// </summary>
bool GameBallSelectionState::CancelShotPreparation()
{
	if (!IsShotReady())
	{
		return false;
	}
	m_FlowState = FlowState::Showing;
	m_HandHiddenRatio = 1.0f;
	return true;
}

/// <summary>
/// 球選択を操作できる間に、画面側で選ばれた有効な候補を反映する。
/// </summary>
bool GameBallSelectionState::ApplyPresentationSelection(
	int selectedOfferIndex,
	int /*legacyHeldOfferIndex*/,
	int offerCount)
{
	if (!IsSelectionInteractive())
	{
		return false;
	}
	if (!SelectOffer(selectedOfferIndex, offerCount))
	{
		return false;
	}
	return true;
}
