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
		m_Settings.collisionImpactEffectsEnabled = settings.value(
			"collision_impact_effects_enabled",
			m_Settings.collisionImpactEffectsEnabled);
		m_Settings.heavyChainEffectsEnabled = settings.value(
			"heavy_chain_effects_enabled",
			m_Settings.heavyChainEffectsEnabled);
		m_Settings.enemyHitFlashEnabled = settings.value(
			"enemy_hit_flash_enabled",
			m_Settings.enemyHitFlashEnabled);
		m_Settings.defeatVfxEnabled = settings.value(
			"defeat_vfx_enabled",
			m_Settings.defeatVfxEnabled);
		m_Settings.multiDefeatEffectsEnabled = settings.value(
			"multi_defeat_effects_enabled",
			m_Settings.multiDefeatEffectsEnabled);
		m_Settings.frameTimeMeasurementEnabled = settings.value(
			"frame_time_measurement_enabled",
			m_Settings.frameTimeMeasurementEnabled);
		m_Settings.decorativeRingAmount = std::clamp(
			settings.value(
				"decorative_ring_amount",
				m_Settings.decorativeRingAmount),
			0,
			3);
		m_Settings.enemyHpDisplay.mode = static_cast<EnemyHpDisplayMode>(
			std::clamp(
				settings.value(
					"enemy_hp_display_mode",
					static_cast<int>(m_Settings.enemyHpDisplay.mode)),
				static_cast<int>(EnemyHpDisplayMode::OverheadBar),
				static_cast<int>(EnemyHpDisplayMode::Hidden)));
		m_Settings.enemyHpDisplay.ringOffsetX = std::clamp(
			settings.value(
				"enemy_hp_ring_offset_x",
				m_Settings.enemyHpDisplay.ringOffsetX),
			-40.0f,
			40.0f);
		m_Settings.enemyHpDisplay.ringOffsetY = std::clamp(
			settings.value(
				"enemy_hp_ring_offset_y",
				m_Settings.enemyHpDisplay.ringOffsetY),
			-40.0f,
			40.0f);
		m_Settings.enemyHpDisplay.ringDiameterScale = std::clamp(
			settings.value(
				"enemy_hp_ring_diameter_scale",
				m_Settings.enemyHpDisplay.ringDiameterScale),
			0.50f,
			1.30f);
		m_Settings.enemyHpDisplay.ringThickness = std::clamp(
			settings.value(
				"enemy_hp_ring_thickness",
				m_Settings.enemyHpDisplay.ringThickness),
			2.0f,
			8.0f);
		m_Settings.enemyHpDisplay.ringOpacity = std::clamp(
			settings.value(
				"enemy_hp_ring_opacity",
				m_Settings.enemyHpDisplay.ringOpacity),
			0.35f,
			1.0f);
		m_Settings.enemyHpDisplay.ringDrawLayer =
			static_cast<EnemyHpRingDrawLayer>(std::clamp(
				settings.value(
					"enemy_hp_ring_draw_layer",
					static_cast<int>(
						m_Settings.enemyHpDisplay.ringDrawLayer)),
				static_cast<int>(EnemyHpRingDrawLayer::Background),
				static_cast<int>(EnemyHpRingDrawLayer::Foreground)));
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
		{ "schema_version", 2 },
		{ "bgm_volume", m_Settings.bgmVolume },
		{ "se_volume", m_Settings.seVolume },
		{ "vibration_enabled", m_Settings.vibrationEnabled },
		{ "screen_flash_enabled", m_Settings.screenFlashEnabled },
		{ "camera_shake_enabled", m_Settings.cameraShakeEnabled },
		{ "collision_impact_effects_enabled",
			m_Settings.collisionImpactEffectsEnabled },
		{ "heavy_chain_effects_enabled",
			m_Settings.heavyChainEffectsEnabled },
		{ "enemy_hit_flash_enabled", m_Settings.enemyHitFlashEnabled },
		{ "defeat_vfx_enabled", m_Settings.defeatVfxEnabled },
		{ "multi_defeat_effects_enabled",
			m_Settings.multiDefeatEffectsEnabled },
		{ "frame_time_measurement_enabled",
			m_Settings.frameTimeMeasurementEnabled },
		{ "decorative_ring_amount", m_Settings.decorativeRingAmount },
		{ "enemy_hp_display_mode",
			static_cast<int>(m_Settings.enemyHpDisplay.mode) },
		{ "enemy_hp_ring_offset_x", m_Settings.enemyHpDisplay.ringOffsetX },
		{ "enemy_hp_ring_offset_y", m_Settings.enemyHpDisplay.ringOffsetY },
		{ "enemy_hp_ring_diameter_scale",
			m_Settings.enemyHpDisplay.ringDiameterScale },
		{ "enemy_hp_ring_thickness",
			m_Settings.enemyHpDisplay.ringThickness },
		{ "enemy_hp_ring_opacity", m_Settings.enemyHpDisplay.ringOpacity },
		{ "enemy_hp_ring_draw_layer",
			static_cast<int>(m_Settings.enemyHpDisplay.ringDrawLayer) },
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
