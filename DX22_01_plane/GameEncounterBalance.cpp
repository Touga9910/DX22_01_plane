#include "GameEncounterBalance.h"

#include "json/json.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

void GameEncounterBalance::Load(const std::string& filePath)
{
	std::ifstream file(filePath);
	if (!file)
	{
		std::cout << "[EncounterBalance] Config not found: "
			<< filePath << std::endl;
		return;
	}

	try
	{
		nlohmann::json config;
		file >> config;
		m_EnemyThreatCosts.clear();
		m_StageThreatTargets.clear();
		const nlohmann::json enemyCosts = config.value(
			"enemy_costs",
			nlohmann::json::object());
		for (auto iterator = enemyCosts.begin();
			iterator != enemyCosts.end(); ++iterator)
		{
			if (iterator.value().is_number())
			{
				m_EnemyThreatCosts[iterator.key()] =
					(std::max)(0.0f, iterator.value().get<float>());
			}
		}
		const nlohmann::json stageTargets = config.value(
			"stage_target_budgets",
			nlohmann::json::object());
		for (auto iterator = stageTargets.begin();
			iterator != stageTargets.end(); ++iterator)
		{
			if (iterator.value().is_number())
			{
				m_StageThreatTargets[iterator.key()] =
					(std::max)(0.0f, iterator.value().get<float>());
			}
		}
		const nlohmann::json layouts = config.value(
			"layout_multipliers",
			nlohmann::json::object());
		m_StageDataLayoutMultiplier =
			(std::max)(0.1f, layouts.value("stage_data", 1.0f));
		m_DenseLayoutMultiplier =
			(std::max)(0.1f, layouts.value("dense_auto_layout", 1.25f));
		m_McpLayoutMultiplier =
			(std::max)(0.1f, layouts.value("mcp_override", 1.0f));
	}
	catch (const nlohmann::json::exception& error)
	{
		std::cerr << "[EncounterBalance] Invalid config: "
			<< error.what() << std::endl;
	}
}

EncounterThreatSnapshot GameEncounterBalance::Evaluate(
	const EncounterThreatInput& input) const
{
	EncounterThreatSnapshot snapshot;
	snapshot.layoutSource = input.usesMcpOverride
		? "mcp_override"
		: (input.enemyIds.size() >= 4
			? "dense_auto_layout"
			: "stage_data");

	for (const std::string& enemyId : input.enemyIds)
	{
		const auto cost = m_EnemyThreatCosts.find(enemyId);
		snapshot.baseBudget += cost != m_EnemyThreatCosts.end()
			? cost->second
			: 10.0f;
	}

	snapshot.layoutMultiplier =
		snapshot.layoutSource == "dense_auto_layout"
		? m_DenseLayoutMultiplier
		: (snapshot.layoutSource == "mcp_override"
			? m_McpLayoutMultiplier
			: m_StageDataLayoutMultiplier);
	snapshot.actualBudget =
		std::round(snapshot.baseBudget * snapshot.layoutMultiplier * 100.0f) /
		100.0f;

	const auto target = m_StageThreatTargets.find(input.stageId);
	if (target != m_StageThreatTargets.end())
	{
		snapshot.targetBudget = target->second;
	}
	return snapshot;
}
