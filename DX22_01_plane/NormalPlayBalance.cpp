#include "NormalPlayBalance.h"

#pragma execution_character_set("utf-8")

#include "json/json.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>

namespace
{
	bool IsValidProgressRange(int minimumProgress, int maximumProgress)
	{
		return minimumProgress >= 1 && maximumProgress >= minimumProgress;
	}
}

NormalPlayBalance::NormalPlayBalance()
{
	ResetToSafeDefaults();
}

void NormalPlayBalance::ResetToSafeDefaults()
{
	m_LoadedFromData = false;
	m_AttackBudgetRanges = {
		{ 1, 6, 4, {} },
		{ 7, (std::numeric_limits<int>::max)(), 5, {} },
	};
	m_StagePools = {
		{ "opening", 1, 2,
			{ "normal_001", "normal_002" } },
		{ "early_setup", 3, 4,
			{ "normal_003", "board_impact_brittle_wall" } },
		{ "early_synergy", 5, 6,
			{ "board_swarm_dense6", "board_impact_brittle_wall",
			  "board_resonance_cluster" } },
		{ "mid_foundation", 7, 8,
			{ "normal_003", "normal_004", "board_swarm_dense6",
			  "board_impact_brittle_wall", "board_resonance_cluster" } },
		{ "mid_six_enemy", 9, 9,
			{ "normal_004", "board_swarm_dense6",
			  "board_resonance_cluster", "normal_swarm_center",
			  "normal_brittle_wall" } },
		{ "mid_control", 10, 10,
			{ "board_resonance_cluster", "normal_swarm_center",
			  "normal_brittle_wall", "board_control_composite" } },
		{ "late_eight_enemy", 11, 11,
			{ "normal_swarm_center", "normal_brittle_wall",
			  "board_control_composite", "normal_dual_threat" } },
		{ "late_resonance", 12, 12,
			{ "board_control_composite", "normal_dual_threat",
			  "normal_resonance_guard" } },
		{ "late_ten_enemy", 13, 13,
			{ "board_control_composite", "normal_dual_threat",
			  "normal_resonance_guard", "normal_resonant_swarm" } },
		{ "final_approach", 14, (std::numeric_limits<int>::max)(),
			{ "normal_dual_threat", "normal_resonance_guard",
			  "normal_resonant_swarm", "normal_triple_encirclement" } },
	};
}

void NormalPlayBalance::Load(const std::string& filePath)
{
	ResetToSafeDefaults();
	std::ifstream file(filePath);
	if (!file)
	{
		std::cerr << "[NormalPlayBalance] Config not found; safe defaults remain: "
			<< filePath << std::endl;
		return;
	}

	try
	{
		nlohmann::json config;
		file >> config;

		std::vector<NormalPlayProgressRange> attackBudgetRanges;
		for (const nlohmann::json& range :
			config.value("enemy_action_budgets", nlohmann::json::array()))
		{
			if (!range.is_object())
			{
				continue;
			}
			const int minimumProgress = range.value("min_progress", 0);
			const int maximumProgress = range.value("max_progress", 0);
			const int budget = range.value("maximum_budget", -1);
			if (IsValidProgressRange(minimumProgress, maximumProgress) &&
				budget >= 0)
			{
				attackBudgetRanges.push_back({
					minimumProgress, maximumProgress, budget, {} });
			}
		}

		std::vector<StagePool> stagePools;
		for (const nlohmann::json& pool :
			config.value("normal_stage_pools", nlohmann::json::array()))
		{
			if (!pool.is_object())
			{
				continue;
			}
			StagePool parsed;
			parsed.id = pool.value("id", std::string());
			parsed.minimumProgress = pool.value("min_progress", 0);
			parsed.maximumProgress = pool.value("max_progress", 0);
			if (pool.contains("stage_ids") && pool["stage_ids"].is_array())
			{
				for (const nlohmann::json& stageId : pool["stage_ids"])
				{
					if (stageId.is_string() && !stageId.get<std::string>().empty())
					{
						parsed.stageIds.push_back(stageId.get<std::string>());
					}
				}
			}
			if (!parsed.id.empty() &&
				IsValidProgressRange(
					parsed.minimumProgress, parsed.maximumProgress) &&
				!parsed.stageIds.empty())
			{
				stagePools.push_back(std::move(parsed));
			}
		}

		if (attackBudgetRanges.empty() || stagePools.empty())
		{
			std::cerr << "[NormalPlayBalance] No usable ranges; safe defaults remain: "
				<< filePath << std::endl;
			return;
		}

		m_AttackBudgetRanges = std::move(attackBudgetRanges);
		m_StagePools = std::move(stagePools);
		m_LoadedFromData = true;
		std::cout << "[NormalPlayBalance] Loaded " << m_StagePools.size()
			<< " stage pools and " << m_AttackBudgetRanges.size()
			<< " attack-budget ranges" << std::endl;
	}
	catch (const nlohmann::json::exception& error)
	{
		std::cerr << "[NormalPlayBalance] Invalid config; safe defaults remain: "
			<< error.what() << std::endl;
	}
}

int NormalPlayBalance::GetEnemyActionMaximumBudget(int progress) const
{
	const auto found = std::find_if(
		m_AttackBudgetRanges.begin(),
		m_AttackBudgetRanges.end(),
		[progress](const NormalPlayProgressRange& range)
		{
			return progress >= range.minimumProgress &&
				progress <= range.maximumProgress;
		});
	return found != m_AttackBudgetRanges.end()
		? found->enemyActionMaximumBudget
		: -1;
}

const NormalPlayBalance::StagePool*
NormalPlayBalance::FindStagePool(int progress) const
{
	const auto found = std::find_if(
		m_StagePools.begin(),
		m_StagePools.end(),
		[progress](const StagePool& pool)
		{
			return progress >= pool.minimumProgress &&
				progress <= pool.maximumProgress;
		});
	return found != m_StagePools.end() ? &*found : nullptr;
}

std::vector<std::string>
NormalPlayBalance::GetAllowedNormalStageIds(int progress) const
{
	const StagePool* pool = FindStagePool(progress);
	return pool != nullptr ? pool->stageIds : std::vector<std::string>{};
}

std::string NormalPlayBalance::GetStagePoolId(int progress) const
{
	const StagePool* pool = FindStagePool(progress);
	return pool != nullptr ? pool->id : std::string();
}
