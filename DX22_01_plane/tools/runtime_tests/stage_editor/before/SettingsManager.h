#pragma once

#include <string>

struct GameSettings
{
	float bgmVolume = 0.8f;
	float seVolume = 0.8f;
	bool vibrationEnabled = true;
	bool screenFlashEnabled = true;
	bool cameraShakeEnabled = true;
	bool fullscreen = false;
	int resolutionIndex = 0;
};

class SettingsManager final
{
public:
	bool Load(const std::string& path = "saves/settings.json");
	bool Save(const std::string& path = "saves/settings.json");
	bool SaveIfDirty(const std::string& path = "saves/settings.json");
	void ApplyDisplaySettings();

	const GameSettings& Get() const { return m_Settings; }
	GameSettings& Edit() { return m_Settings; }
	void MarkDirty() { m_Dirty = true; }
	bool IsDirty() const { return m_Dirty; }

	static int GetResolutionCount();
	static const char* GetResolutionLabel(int index);

private:
	GameSettings m_Settings{};
	bool m_Dirty = false;
};
