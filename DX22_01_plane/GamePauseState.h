#pragma once

// Owns pause-menu state and the one-shot UI requests consumed by Game's update
// loop. Keeping this state behind a narrow boundary prevents UI details from
// becoming part of Game's public header.
class GamePauseState final
{
private:
	bool m_Paused = false;
	bool m_PauseToggleRequested = false;
	bool m_ManualSaveRequested = false;
	bool m_FullscreenToggleRequested = false;
	bool m_ReturnToTitleConfirmationActive = false;
	bool m_DisplayApplyRequested = false;

public:
	bool IsPaused() const;
	void SetPaused(bool paused);
	bool TogglePaused();
	void ResumeAndDismissConfirmation();

	void RequestPauseToggle();
	bool ConsumePauseToggleRequest();
	void RequestManualSave();
	bool ConsumeManualSaveRequest();
	void RequestFullscreenToggle();
	bool ConsumeFullscreenToggleRequest();

	bool IsReturnToTitleConfirmationActive() const;
	void RequestReturnToTitleConfirmation();
	void CancelReturnToTitleConfirmation();

	void RequestDisplayApply();
	bool ConsumeDisplayApplyRequest();
};
