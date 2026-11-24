#include "GamePauseState.h"

#include <utility>

/// <summary>
/// 保存されているポーズ状態を返す。
/// </summary>
bool GamePauseState::IsPaused() const
{
	return m_Paused;
}

/// <summary>
/// ポーズ状態を指定値に設定する。
/// </summary>
void GamePauseState::SetPaused(bool paused)
{
	m_Paused = paused;
}

/// <summary>
/// ポーズを切り替えて確認表示を閉じ、切り替え前の状態を返す。
/// </summary>
bool GamePauseState::TogglePaused()
{
	const bool wasPaused = m_Paused;
	m_Paused = !m_Paused;
	m_ReturnToTitleConfirmationActive = false;
	return wasPaused;
}

/// <summary>
/// ポーズを解除し、タイトルへ戻る確認表示を閉じる。
/// </summary>
void GamePauseState::ResumeAndDismissConfirmation()
{
	m_Paused = false;
	m_ReturnToTitleConfirmationActive = false;
}

/// <summary>
/// ポーズ切り替えの要求を記録する。
/// </summary>
void GamePauseState::RequestPauseToggle()
{
	m_PauseToggleRequested = true;
}

/// <summary>
/// ポーズ切り替えの要求があったかを返して、要求を解除する。
/// </summary>
bool GamePauseState::ConsumePauseToggleRequest()
{
	return std::exchange(m_PauseToggleRequested, false);
}

/// <summary>
/// 手動保存の要求を記録する。
/// </summary>
void GamePauseState::RequestManualSave()
{
	m_ManualSaveRequested = true;
}

/// <summary>
/// 手動保存の要求があったかを返して、要求を解除する。
/// </summary>
bool GamePauseState::ConsumeManualSaveRequest()
{
	return std::exchange(m_ManualSaveRequested, false);
}

/// <summary>
/// 全画面表示切り替えの要求を記録する。
/// </summary>
void GamePauseState::RequestFullscreenToggle()
{
	m_FullscreenToggleRequested = true;
}

/// <summary>
/// 全画面表示切り替えの要求があったかを返して、要求を解除する。
/// </summary>
bool GamePauseState::ConsumeFullscreenToggleRequest()
{
	return std::exchange(m_FullscreenToggleRequested, false);
}

/// <summary>
/// タイトルへ戻る確認表示が有効かを返す。
/// </summary>
bool GamePauseState::IsReturnToTitleConfirmationActive() const
{
	return m_ReturnToTitleConfirmationActive;
}

/// <summary>
/// タイトルへ戻る確認表示を開く。
/// </summary>
void GamePauseState::RequestReturnToTitleConfirmation()
{
	m_ReturnToTitleConfirmationActive = true;
}

/// <summary>
/// タイトルへ戻る確認表示を閉じる。
/// </summary>
void GamePauseState::CancelReturnToTitleConfirmation()
{
	m_ReturnToTitleConfirmationActive = false;
}

/// <summary>
/// 表示設定の適用要求を記録する。
/// </summary>
void GamePauseState::RequestDisplayApply()
{
	m_DisplayApplyRequested = true;
}

/// <summary>
/// 表示設定の適用要求があったかを返して、要求を解除する。
/// </summary>
bool GamePauseState::ConsumeDisplayApplyRequest()
{
	return std::exchange(m_DisplayApplyRequested, false);
}
