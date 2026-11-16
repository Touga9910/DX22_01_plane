#include "Game.h"
#include "GameSettingsRuntime.h"

#pragma execution_character_set("utf-8")

bool Game::IsVibrationEnabled() const
{
	return m_SettingsRuntime->IsVibrationEnabled();
}

bool Game::IsScreenFlashEnabled() const
{
	return m_SettingsRuntime->IsScreenFlashEnabled();
}

bool Game::IsCameraShakeEnabled() const
{
	return m_SettingsRuntime->IsCameraShakeEnabled();
}

bool Game::IsCollisionImpactEffectsEnabled() const
{
	return m_SettingsRuntime->IsCollisionImpactEffectsEnabled();
}

bool Game::IsHeavyChainEffectsEnabled() const
{
	return m_SettingsRuntime->IsHeavyChainEffectsEnabled();
}

bool Game::IsEnemyHitFlashEnabled() const
{
	return m_SettingsRuntime->IsEnemyHitFlashEnabled();
}

bool Game::IsDefeatVfxEnabled() const
{
	return m_SettingsRuntime->IsDefeatVfxEnabled();
}

bool Game::IsMultiDefeatEffectsEnabled() const
{
	return m_SettingsRuntime->IsMultiDefeatEffectsEnabled();
}

bool Game::IsFrameTimeMeasurementEnabled() const
{
	return m_SettingsRuntime->IsFrameTimeMeasurementEnabled();
}

int Game::GetDecorativeRingAmount() const
{
	return m_SettingsRuntime->GetDecorativeRingAmount();
}

const EnemyHpDisplaySettings& Game::GetEnemyHpDisplaySettings() const
{
	return m_SettingsRuntime->GetEnemyHpDisplaySettings();
}

float Game::GetSeVolume() const
{
	return m_SettingsRuntime != nullptr
		? m_SettingsRuntime->Current().seVolume
		: 0.0f;
}
