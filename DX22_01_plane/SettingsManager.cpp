#include "SettingsManager.h"

#include "Application.h"
#include "json/json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>

namespace
{
	constexpr int kResolutionCount = 3;
	constexpr int kResolutionWidths[kResolutionCount] = { 1280, 1600, 1920 };
	constexpr int kResolutionHeights[kResolutionCount] = { 720, 900, 1080 };
	constexpr const char* kResolutionLabels[kResolutionCount] =
	{
		"1280 x 720",
		"1600 x 900",
		"1920 x 1080",
	};
}

bool SettingsManager::Load(const std::string& path)
{
	std::ifstream file(path);
	if (!file)
	{
		return false;
	}
	try
	{
		nlohmann::json settings;
		file >> settings;
		m_Settings.bgmVolume = std::clamp(
			settings.value("bgm_volume", m_Settings.bgmVolume),
			0.0f,
			1.0f);
		m_Settings.seVolume = std::clamp(
			settings.value("se_volume", m_Settings.seVolume),
			0.0f,
			1.0f);
		m_Settings.vibrationEnabled = settings.value(
			"vibration_enabled",
			m_Settings.vibrationEnabled);
		m_Settings.screenFlashEnabled = settings.value(
			"screen_flash_enabled",
			m_Settings.screenFlashEnabled);
		m_Settings.cameraShakeEnabled = settings.value(
			"camera_shake_enabled",
			m_Settings.cameraShakeEnabled);
		m_Settings.fullscreen = settings.value(
			"fullscreen",
			m_Settings.fullscreen);
		m_Settings.resolutionIndex = std::clamp(
			settings.value("resolution_index", m_Settings.resolutionIndex),
			0,
			kResolutionCount - 1);
		m_Dirty = false;
		return true;
	}
	catch (const std::exception&)
	{
		m_Settings = GameSettings{};
		m_Dirty = false;
		return false;
	}
}

bool SettingsManager::Save(const std::string& path)
{
	std::error_code error;
	const std::filesystem::path settingsPath(path);
	if (settingsPath.has_parent_path())
	{
		std::filesystem::create_directories(
			settingsPath.parent_path(),
			error);
	}
	std::ofstream file(settingsPath, std::ios::trunc);
	if (!file)
	{
		return false;
	}
	file << std::setw(2) << nlohmann::json{
		{ "schema_version", 1 },
		{ "bgm_volume", m_Settings.bgmVolume },
		{ "se_volume", m_Settings.seVolume },
		{ "vibration_enabled", m_Settings.vibrationEnabled },
		{ "screen_flash_enabled", m_Settings.screenFlashEnabled },
		{ "camera_shake_enabled", m_Settings.cameraShakeEnabled },
		{ "fullscreen", m_Settings.fullscreen },
		{ "resolution_index", m_Settings.resolutionIndex },
	} << '\n';
	if (!file)
	{
		return false;
	}
	m_Dirty = false;
	return true;
}

bool SettingsManager::SaveIfDirty(const std::string& path)
{
	return !m_Dirty || Save(path);
}

void SettingsManager::ApplyDisplaySettings()
{
	m_Settings.resolutionIndex = std::clamp(
		m_Settings.resolutionIndex,
		0,
		kResolutionCount - 1);
	Application::SetDisplayMode(
		static_cast<unsigned int>(
			kResolutionWidths[m_Settings.resolutionIndex]),
		static_cast<unsigned int>(
			kResolutionHeights[m_Settings.resolutionIndex]),
		m_Settings.fullscreen);
}

int SettingsManager::GetResolutionCount()
{
	return kResolutionCount;
}

const char* SettingsManager::GetResolutionLabel(int index)
{
	index = std::clamp(index, 0, kResolutionCount - 1);
	return kResolutionLabels[index];
}
