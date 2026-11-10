#include "BalanceValidationController.h"

#pragma execution_character_set("utf-8")

#include "json/json.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>

void BalanceValidationController::LoadConfig(
	const std::string& filePath,
	const std::string& baselineDifficultyProfile)
{
	std::ifstream file(filePath);
	if (!file)
	{
		std::cout << "[BalanceValidation] Config not found: "
			<< filePath << std::endl;
		return;
	}

	try
	{
		nlohmann::json config;
		file >> config;
		m_Enabled = config.value("enabled", false);
		m_FixedStageSchedule = config.value("fixed_stage_schedule", true);
		m_EnduranceMode = config.value("endurance_mode", false);
		m_Seed = config.value("random_seed", 20260807u);
		m_Seeds.clear();
		if (config.contains("random_seeds") && config["random_seeds"].is_array())
		{
			for (const nlohmann::json& seed : config["random_seeds"])
			{
				if (!seed.is_number_unsigned() && !seed.is_number_integer())
				{
					continue;
				}
				const long long value = seed.get<long long>();
				if (value >= 0 && value <= 0xffffffffll)
				{
					m_Seeds.push_back(static_cast<std::uint32_t>(value));
				}
			}
		}
		if (m_Seeds.empty())
		{
			m_Seeds.push_back(m_Seed);
		}

		m_ExperimentId = config.value(
			"experiment_id", std::string("fixed_baseline"));
		m_MaximumClearedStages = (std::max)(
			0,
			config.value(
				"maximum_cleared_stages_per_run",
				m_MaximumClearedStages));
		m_Variants.clear();
		if (config.contains("variants") && config["variants"].is_array())
		{
			for (const nlohmann::json& variant : config["variants"])
			{
				if (!variant.is_object())
				{
					continue;
				}
				const std::string id = variant.value("id", std::string());
				if (!id.empty())
				{
					m_Variants.push_back({
						id,
						variant.value("adjusted_early_stages", false),
						variant.value("adjusted_mid_stages", false),
						variant.value("tactical_ai", true),
						variant.value(
							"tactical_ai_profile",
							std::string("tactics_v1")),
						variant.value("early_attack_budget", -1),
						variant.value("mid_attack_budget", -1),
					});
				}
			}
		}
		if (m_Variants.empty())
		{
			m_Variants.push_back({ m_ExperimentId });
		}
		m_VariantIndex = 0;
		m_CurrentVariantId = m_Variants.front().id;

		if (m_Enabled && config.contains("baseline_profile") &&
			config["baseline_profile"].is_string())
		{
			const std::string requestedProfile =
				config["baseline_profile"].get<std::string>();
			if (requestedProfile != baselineDifficultyProfile)
			{
				std::cout << "[BalanceValidation] baseline_profile is "
					<< requestedProfile
					<< "; set the same selected_profile in "
					<< "difficulty_profiles.json to apply it."
					<< std::endl;
			}
		}
		std::cout << "[BalanceValidation] "
			<< (m_Enabled ? "Enabled" : "Disabled")
			<< " / Seed=" << m_Seed
			<< " / Experiment=" << m_ExperimentId
			<< " / Variants=" << m_Variants.size()
			<< std::endl;
	}
	catch (const nlohmann::json::exception& error)
	{
		std::cerr << "[BalanceValidation] Invalid config: "
			<< error.what() << std::endl;
	}
}

BalanceValidationRun BalanceValidationController::OnRunStarted(
	const std::string& forcedVariant,
	std::optional<std::uint32_t> forcedSeed)
{
	BalanceValidationRun result;
	result.enabled = m_Enabled;
	if (!forcedVariant.empty())
	{
		const auto found = std::find_if(
			m_Variants.begin(),
			m_Variants.end(),
			[&forcedVariant](const BalanceValidationVariant& variant)
			{
				return variant.id == forcedVariant;
			});
		if (found != m_Variants.end())
		{
			m_VariantIndex = static_cast<std::uint32_t>(
				std::distance(m_Variants.begin(), found));
			m_CurrentVariantId = found->id;
		}
	}
	if (m_Enabled)
	{
		const std::uint32_t seedCount = static_cast<std::uint32_t>(
			(std::max)(std::size_t{ 1 }, m_Seeds.size()));
		const std::uint32_t variantCount = static_cast<std::uint32_t>(
			(std::max)(std::size_t{ 1 }, m_Variants.size()));
		m_SeedIndex = m_Seeds.empty() ? 0 : m_RunCounter % seedCount;
		m_VariantIndex = (m_RunCounter / seedCount) % variantCount;
		if (!forcedVariant.empty())
		{
			const auto found = std::find_if(
				m_Variants.begin(),
				m_Variants.end(),
				[&forcedVariant](const BalanceValidationVariant& variant)
				{
					return variant.id == forcedVariant;
				});
			if (found != m_Variants.end())
			{
				m_VariantIndex = static_cast<std::uint32_t>(
					std::distance(m_Variants.begin(), found));
			}
		}
		if (!m_Variants.empty())
		{
			const BalanceValidationVariant& variant = m_Variants[m_VariantIndex];
			m_CurrentVariantId = variant.id;
		}
		else
		{
			m_CurrentVariantId = m_ExperimentId;
		}
		result.seed = m_Seeds.empty() ? m_Seed : m_Seeds[m_SeedIndex];
		++m_RunCounter;
	}

	if (forcedSeed.has_value())
	{
		result.seed = forcedSeed.value();
		const auto found = std::find(m_Seeds.begin(), m_Seeds.end(), result.seed);
		if (found != m_Seeds.end())
		{
			m_SeedIndex = static_cast<std::uint32_t>(
				std::distance(m_Seeds.begin(), found));
		}
	}
	return result;
}

bool BalanceValidationController::HasVariant(const std::string& id) const
{
	return std::any_of(
		m_Variants.begin(),
		m_Variants.end(),
		[&id](const BalanceValidationVariant& variant)
		{
			return variant.id == id;
		});
}

const BalanceValidationVariant*
BalanceValidationController::GetCurrentVariant() const
{
	const auto found = std::find_if(
		m_Variants.begin(),
		m_Variants.end(),
		[this](const BalanceValidationVariant& variant)
		{
			return variant.id == m_CurrentVariantId;
		});
	return found != m_Variants.end() ? &*found : nullptr;
}

bool BalanceValidationController::UsesAdjustedEarlyStages() const
{
	const BalanceValidationVariant* variant = GetCurrentVariant();
	return variant != nullptr && variant->adjustedEarlyStages;
}

bool BalanceValidationController::UsesAdjustedMidStages() const
{
	const BalanceValidationVariant* variant = GetCurrentVariant();
	return variant != nullptr && variant->adjustedMidStages;
}

bool BalanceValidationController::UsesTacticalAi() const
{
	const BalanceValidationVariant* variant = GetCurrentVariant();
	return variant == nullptr || variant->tacticalAi;
}

const std::string& BalanceValidationController::GetTacticalAiProfile() const
{
	const BalanceValidationVariant* variant = GetCurrentVariant();
	static const std::string fallback = "tactics_v1";
	return variant != nullptr ? variant->tacticalAiProfile : fallback;
}

int BalanceValidationController::GetAttackBudget(int progress) const
{
	const BalanceValidationVariant* variant = GetCurrentVariant();
	if (variant == nullptr)
	{
		return -1;
	}
	if (progress >= 1 && progress <= 6)
	{
		return variant->earlyAttackBudget;
	}
	if (progress >= 7 && progress <= 11)
	{
		return variant->midAttackBudget;
	}
	return -1;
}

std::vector<std::string>
BalanceValidationController::GetAllowedNormalStageIds(int progress) const
{
	if (UsesAdjustedEarlyStages() && progress >= 1 && progress <= 6)
	{
		if (progress <= 2)
		{
			return { "normal_001", "normal_002" };
		}
		if (progress <= 4)
		{
			return { "normal_003", "board_impact_brittle_wall" };
		}
		if (progress == 5)
		{
			return { "board_swarm_dense6", "board_impact_brittle_wall" };
		}
		return {
			"board_swarm_dense6",
			"board_impact_brittle_wall",
			"board_resonance_cluster",
		};
	}

	if (!UsesAdjustedMidStages() || progress < 7 || progress > 11)
	{
		return {};
	}
	std::vector<std::string> allowed{
		"normal_003",
		"normal_004",
		"board_swarm_dense6",
		"board_impact_brittle_wall",
		"board_resonance_cluster",
	};
	if (progress >= 9)
	{
		allowed.push_back("board_control_composite");
	}
	if (progress >= 10)
	{
		allowed.push_back("normal_resonance_guard");
	}
	if (progress >= 11)
	{
		allowed.push_back("normal_resonant_swarm");
	}
	return allowed;
}
