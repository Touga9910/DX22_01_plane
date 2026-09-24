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

		std::cout << "[DifficultyScaling] fixed progression scaling="
			<< (m_ProgressionScalingEnabled ? "enabled" : "disabled")
			<< std::endl;
	}
	catch (const nlohmann::json::exception& error)
	{
		std::cerr << "[DifficultyScaling] Invalid config: "
			<< error.what() << std::endl;
	}
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
		// デバッグで入力した性能は通常用の敵性能上限で丸めず、
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
