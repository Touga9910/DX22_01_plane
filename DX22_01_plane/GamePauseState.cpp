#include "GamePauseState.h"

#include <utility>

bool GamePauseState::IsPaused() const
{
	return m_Paused;
}

void GamePauseState::SetPaused(bool paused)
{
	m_Paused = paused;
}

bool GamePauseState::TogglePaused()
{
	const bool wasPaused = m_Paused;
	m_Paused = !m_Paused;
	m_ReturnToTitleConfirmationActive = false;
	return wasPaused;
}

void GamePauseState::ResumeAndDismissConfirmation()
{
	m_Paused = false;
	m_ReturnToTitleConfirmationActive = false;
}

void GamePauseState::RequestPauseToggle()
{
	m_PauseToggleRequested = true;
}

bool GamePauseState::ConsumePauseToggleRequest()
{
	return std::exchange(m_PauseToggleRequested, false);
}

void GamePauseState::RequestManualSave()
{
	m_ManualSaveRequested = true;
}

bool GamePauseState::ConsumeManualSaveRequest()
{
	return std::exchange(m_ManualSaveRequested, false);
}

void GamePauseState::RequestFullscreenToggle()
{
	m_FullscreenToggleRequested = true;
}

bool GamePauseState::ConsumeFullscreenToggleRequest()
{
	return std::exchange(m_FullscreenToggleRequested, false);
}

bool GamePauseState::IsReturnToTitleConfirmationActive() const
{
	return m_ReturnToTitleConfirmationActive;
}

void GamePauseState::RequestReturnToTitleConfirmation()
{
	m_ReturnToTitleConfirmationActive = true;
}

void GamePauseState::CancelReturnToTitleConfirmation()
{
	m_ReturnToTitleConfirmationActive = false;
}

void GamePauseState::RequestDisplayApply()
{
	m_DisplayApplyRequested = true;
}

bool GamePauseState::ConsumeDisplayApplyRequest()
{
	return std::exchange(m_DisplayApplyRequested, false);
}
