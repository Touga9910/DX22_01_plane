#include "GameSettingsRuntime.h"

void GameSettingsRuntime::Initialize()
{
	m_Manager.Load();
	m_Manager.ApplyDisplaySettings();
}

void GameSettingsRuntime::ApplyDisplaySettings()
{
	m_Manager.ApplyDisplaySettings();
}

void GameSettingsRuntime::ToggleFullscreen()
{
	GameSettings& settings = m_Manager.Edit();
	settings.fullscreen = !settings.fullscreen;
	m_Manager.MarkDirty();
	m_Manager.ApplyDisplaySettings();
	m_Manager.SaveIfDirty();
}

void GameSettingsRuntime::SaveIfDirty()
{
	m_Manager.SaveIfDirty();
}

const GameSettings& GameSettingsRuntime::Current() const
{
	return m_Manager.Get();
}

bool GameSettingsRuntime::Replace(const GameSettings& settings)
{
	const bool vibrationTurnedOff =
		m_Manager.Get().vibrationEnabled && !settings.vibrationEnabled;
	m_Manager.Edit() = settings;
	m_Manager.MarkDirty();
	return vibrationTurnedOff;
}

bool GameSettingsRuntime::IsVibrationEnabled() const
{
	return m_Manager.Get().vibrationEnabled;
}

bool GameSettingsRuntime::IsScreenFlashEnabled() const
{
	return m_Manager.Get().screenFlashEnabled;
}

bool GameSettingsRuntime::IsCameraShakeEnabled() const
{
	return m_Manager.Get().cameraShakeEnabled;
}
