#pragma once

#include "SettingsManager.h"

// Owns application settings and their persistence/display side effects so the
// Game coordinator does not expose SettingsManager's storage details.
class GameSettingsRuntime final
{
public:
	void Initialize();
	void ApplyDisplaySettings();
	void ToggleFullscreen();
	void SaveIfDirty();

	const GameSettings& Current() const;
	bool Replace(const GameSettings& settings);

	bool IsVibrationEnabled() const;
	bool IsScreenFlashEnabled() const;
	bool IsCameraShakeEnabled() const;
	bool IsCollisionImpactEffectsEnabled() const;
	bool IsHeavyChainEffectsEnabled() const;
	bool IsEnemyHitFlashEnabled() const;
	bool IsDefeatVfxEnabled() const;
	bool IsMultiDefeatEffectsEnabled() const;
	bool IsFrameTimeMeasurementEnabled() const;
	int GetDecorativeRingAmount() const;
	const EnemyHpDisplaySettings& GetEnemyHpDisplaySettings() const;

private:
	SettingsManager m_Manager;
};
