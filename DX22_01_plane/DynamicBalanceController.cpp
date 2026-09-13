#include "DynamicBalanceController.h"

#pragma execution_character_set("utf-8")

#include "EnemyData.h"
#include "ProgressionProfile.h"
#include "json/json.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

void DynamicBalanceController::LoadConfig(const std::string& filePath)
{
	std::ifstream file(filePath);
	if (!file)
	{
		std::cout << "[DynamicBalance] Config not found: "
			<< filePath << std::endl;
		return;
	}

	try
	{
		nlohmann::json config;
		file >> config;
		// DDAはゲーム方針として廃止した。旧設定にenabled=trueが残っても
		// プレイ結果から敵性能が変化しないよう、常に無効として扱う。
		m_ConfiguredEnabled = false;
		m_Enabled = false;
		m_MinimumLevel = config.value("minimum_level", m_MinimumLevel);
		m_MaximumLevel = config.value("maximum_level", m_MaximumLevel);
		if (m_MinimumLevel > m_MaximumLevel)
		{
			std::swap(m_MinimumLevel, m_MaximumLevel);
		}
		m_InitialLevel = std::clamp(
			config.value("initial_level", m_InitialLevel),
			m_MinimumLevel,
			m_MaximumLevel);
		m_Level = m_InitialLevel;
		m_HpStep = (std::max)(0, config.value("hp_step_per_level", m_HpStep));
		m_AttackStep = (std::max)(0, config.value("attack_step", m_AttackStep));
		m_LevelsPerAttackStep = (std::max)(
			1,
			config.value("levels_per_attack_step", m_LevelsPerAttackStep));
		m_PositiveAttackScalingEnabled = config.value(
			"positive_attack_scaling_enabled",
			m_PositiveAttackScalingEnabled);

		const nlohmann::json progression = config.value(
			"progression_scaling",
			nlohmann::json::object());
		if (progression.is_object())
		{
			m_ProgressionScalingEnabled = progression.value(
				"enabled", m_ProgressionScalingEnabled);
			m_ProgressionHpStart = (std::max)(
				1, progression.value("hp_start_progress", m_ProgressionHpStart));
			m_ProgressionHpInterval = (std::max)(
				1, progression.value("hp_interval", m_ProgressionHpInterval));
			m_ProgressionHpStep = (std::max)(
				0, progression.value("hp_step", m_ProgressionHpStep));
			m_ProgressionHpMaximumDelta = (std::max)(
				0, progression.value("maximum_hp_delta", m_ProgressionHpMaximumDelta));
			m_ProgressionAttackStart = (std::max)(
				1, progression.value("attack_start_progress", m_ProgressionAttackStart));
			m_ProgressionAttackInterval = (std::max)(
				1, progression.value("attack_interval", m_ProgressionAttackInterval));
			m_ProgressionAttackStep = (std::max)(
				0, progression.value("attack_step", m_ProgressionAttackStep));
			m_ProgressionAttackMaximumDelta = (std::max)(
				0, progression.value(
					"maximum_attack_delta", m_ProgressionAttackMaximumDelta));
		}

		m_MinimumEnemyHp = (std::max)(
			1, config.value("minimum_enemy_hp", m_MinimumEnemyHp));
		m_MaximumEnemyHp = (std::max)(
			m_MinimumEnemyHp,
			config.value("maximum_enemy_hp", m_MaximumEnemyHp));
		m_MinimumEnemyAttack = (std::max)(
			0, config.value("minimum_enemy_attack", m_MinimumEnemyAttack));
		m_MaximumEnemyAttack = (std::max)(
			m_MinimumEnemyAttack,
			config.value("maximum_enemy_attack", m_MaximumEnemyAttack));
		m_StrongHpRatio = std::clamp(
			config.value("strong_clear_hp_ratio", m_StrongHpRatio), 0.0f, 1.0f);
		m_WeakHpRatio = std::clamp(
			config.value("weak_clear_hp_ratio", m_WeakHpRatio), 0.0f, 1.0f);
		m_StrongNoHitRate = std::clamp(
			config.value("strong_no_hit_rate", m_StrongNoHitRate), 0.0f, 1.0f);
		m_WeakNoHitRate = std::clamp(
			config.value("weak_no_hit_rate", m_WeakNoHitRate), 0.0f, 1.0f);
		m_TargetShotsPerEnemy = (std::max)(
			0.1f,
			config.value("target_shots_per_enemy", m_TargetShotsPerEnemy));
		m_WeakShotMultiplier = (std::max)(
			1.0f,
			config.value("weak_shot_multiplier", m_WeakShotMultiplier));

		std::cout << "[DynamicBalance] Retired"
			<< " / fixed progression scaling="
			<< (m_ProgressionScalingEnabled ? "enabled" : "disabled")
			<< std::endl;
	}
	catch (const nlohmann::json::exception& error)
	{
		std::cerr << "[DynamicBalance] Invalid config: "
			<< error.what() << std::endl;
	}
}

void DynamicBalanceController::OnRunStarted()
{
	m_Enabled = false;
	m_Level = 0;
	m_AppliedEnabled = false;
	m_AppliedLevel = m_Level;
	m_StageActive = false;
	m_ShotActive = false;
	m_CurrentShotHit = false;
	m_StageShots = 0;
	m_StageNoHitShots = 0;
	m_StageEnemyCount = 0;
	m_LastLevelChange = 0;
	m_LastHpRatio = 1.0f;
	m_LastNoHitRate = 0.0f;
	m_LastShotsPerEnemy = 0.0f;
	m_LastResult = "not_evaluated";
	m_LastReason = "No battle has been evaluated in this run.";
}

void DynamicBalanceController::OnBattleStarted(int enemyCount)
{
	(void)enemyCount;
	m_AppliedEnabled = false;
	m_AppliedLevel = 0;
	m_StageActive = false;
	m_ShotActive = false;
	m_CurrentShotHit = false;
	m_StageShots = 0;
	m_StageNoHitShots = 0;
	m_StageEnemyCount = 0;
}

void DynamicBalanceController::OnShotStarted()
{
	if (!m_StageActive)
	{
		return;
	}
	OnShotFinished();
	++m_StageShots;
	m_ShotActive = true;
	m_CurrentShotHit = false;
}

void DynamicBalanceController::OnHit()
{
	if (m_StageActive && m_ShotActive)
	{
		m_CurrentShotHit = true;
	}
}

void DynamicBalanceController::OnShotFinished()
{
	if (!m_StageActive || !m_ShotActive)
	{
		return;
	}
	if (!m_CurrentShotHit)
	{
		++m_StageNoHitShots;
	}
	m_ShotActive = false;
}

void DynamicBalanceController::OnBattleFinished(
	bool cleared,
	int currentHp,
	int maxHp)
{
	if (!m_StageActive)
	{
		return;
	}
	OnShotFinished();
	const int safeEnemyCount = (std::max)(1, m_StageEnemyCount);
	const float targetShots =
		m_TargetShotsPerEnemy * static_cast<float>(safeEnemyCount);
	m_LastHpRatio = maxHp <= 0
		? 0.0f
		: std::clamp(
			static_cast<float>(currentHp) / static_cast<float>(maxHp),
			0.0f,
			1.0f);
	m_LastNoHitRate = m_StageShots <= 0
		? 0.0f
		: static_cast<float>(m_StageNoHitShots) /
			static_cast<float>(m_StageShots);
	m_LastShotsPerEnemy =
		static_cast<float>(m_StageShots) / static_cast<float>(safeEnemyCount);
	m_LastResult = cleared ? "clear" : "game_over";
	m_LastLevelChange = 0;

	int requestedChange = 0;
	if (!m_Enabled)
	{
		m_LastReason = "Dynamic difficulty was retired; fixed difficulty is active.";
	}
	else if (!cleared)
	{
		requestedChange = -1;
		m_LastReason = "Game over: reduce the next battle difficulty.";
	}
	else
	{
		const bool strongClear =
			m_LastHpRatio >= m_StrongHpRatio &&
			m_LastNoHitRate <= m_StrongNoHitRate &&
			static_cast<float>(m_StageShots) <= targetShots;
		const bool weakClear =
			m_LastHpRatio <= m_WeakHpRatio ||
			m_LastNoHitRate >= m_WeakNoHitRate ||
			static_cast<float>(m_StageShots) >
				targetShots * m_WeakShotMultiplier;
		if (strongClear)
		{
			requestedChange = 1;
			m_LastReason = m_Level >= m_MaximumLevel
				? "Strong clear: assist mode is already at baseline."
				: "Strong clear: reduce assistance for the next battle.";
		}
		else if (weakClear)
		{
			requestedChange = -1;
			m_LastReason =
				"Struggling clear: reduce the next battle difficulty.";
		}
		else
		{
			m_LastReason = "Performance is inside the target range.";
		}
	}

	const int previousLevel = m_Level;
	m_Level = std::clamp(
		m_Level + requestedChange,
		m_MinimumLevel,
		m_MaximumLevel);
	m_LastLevelChange = m_Level - previousLevel;
	m_StageActive = false;
	std::cout << "[DynamicBalance] Result=" << m_LastResult
		<< " HP=" << m_LastHpRatio
		<< " NoHit=" << m_LastNoHitRate
		<< " ShotsPerEnemy=" << m_LastShotsPerEnemy
		<< " Level=" << previousLevel << "->" << m_Level
		<< " Reason=" << m_LastReason << std::endl;
}

int DynamicBalanceController::CalculateAttackModifier(int level) const
{
	if (level > 0 && !m_PositiveAttackScalingEnabled)
	{
		return 0;
	}
	return (level / m_LevelsPerAttackStep) * m_AttackStep;
}

int DynamicBalanceController::CalculateProgressionHpModifier(int progress) const
{
	if (!m_ProgressionScalingEnabled ||
		progress < m_ProgressionHpStart ||
		m_ProgressionHpStep <= 0)
	{
		return 0;
	}
	const int tier = 1 +
		(progress - m_ProgressionHpStart) / m_ProgressionHpInterval;
	return (std::min)(m_ProgressionHpMaximumDelta, tier * m_ProgressionHpStep);
}

int DynamicBalanceController::CalculateProgressionAttackModifier(int progress) const
{
	if (!m_ProgressionScalingEnabled ||
		progress < m_ProgressionAttackStart ||
		m_ProgressionAttackStep <= 0)
	{
		return 0;
	}
	const int tier = 1 +
		(progress - m_ProgressionAttackStart) / m_ProgressionAttackInterval;
	return (std::min)(
		m_ProgressionAttackMaximumDelta,
		tier * m_ProgressionAttackStep);
}

void DynamicBalanceController::ApplyToEnemyData(
	EnemyData& enemyData,
	float baselineHpMultiplier,
	int baselineAttackDelta,
	int progress,
	int ascension,
	bool debugMode) const
{
	if (debugMode)
	{
		// デバッグで入力した性能は通常用のDDA上限で丸めず、
		// 選択したアセンションのみを重ねる。
		enemyData.maxHp = (std::max)(
			1,
			static_cast<int>(std::lround(
				enemyData.maxHp * ProgressionProfile::EnemyHpMultiplier(ascension))));
		enemyData.status.attack = (std::max)(
			0,
			enemyData.status.attack + ProgressionProfile::EnemyAttackBonus(ascension));
		return;
	}
	const bool armorBoss = enemyData.id == "enemy_boss_core";
	if (!armorBoss)
	{
		enemyData.maxHp = std::clamp(
			static_cast<int>(std::lround(
				static_cast<double>(enemyData.maxHp) *
				static_cast<double>(baselineHpMultiplier))) +
				CalculateProgressionHpModifier(progress),
			m_MinimumEnemyHp,
			m_MaximumEnemyHp);
		enemyData.status.attack = std::clamp(
			enemyData.status.attack + baselineAttackDelta +
				CalculateProgressionAttackModifier(progress),
			m_MinimumEnemyAttack,
			m_MaximumEnemyAttack);
	}

	enemyData.maxHp = std::clamp(
		static_cast<int>(std::lround(
			enemyData.maxHp * ProgressionProfile::EnemyHpMultiplier(ascension))),
		m_MinimumEnemyHp,
		m_MaximumEnemyHp);
	enemyData.status.attack = std::clamp(
		enemyData.status.attack + ProgressionProfile::EnemyAttackBonus(ascension),
		m_MinimumEnemyAttack,
		m_MaximumEnemyAttack);
}

void DynamicBalanceController::Set(
	bool enabled,
	bool resetLevel,
	int requestedLevel,
	bool hasRequestedLevel,
	bool lockedOffByValidation)
{
	(void)enabled;
	(void)resetLevel;
	(void)requestedLevel;
	(void)hasRequestedLevel;
	(void)lockedOffByValidation;
	ForceDisabled();
	m_LastReason = "Dynamic difficulty was retired; fixed difficulty is active.";
}

void DynamicBalanceController::ForceDisabled()
{
	m_Enabled = false;
	m_AppliedEnabled = false;
	m_Level = 0;
	m_AppliedLevel = 0;
	m_LastLevelChange = 0;
}

void DynamicBalanceController::RestoreRunState(
	bool enabled,
	int level,
	bool appliedEnabled,
	int appliedLevel,
	const std::string& lastResult,
	const std::string& lastReason)
{
	(void)enabled;
	(void)level;
	(void)appliedEnabled;
	(void)appliedLevel;
	(void)lastReason;
	ForceDisabled();
	m_LastResult = lastResult;
	m_LastReason = "Dynamic difficulty was retired; fixed difficulty is active.";
}
