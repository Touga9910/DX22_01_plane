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
		m_DisableDynamicBalance = true;
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
						true,
					});
				}
			}
		}
		if (m_Variants.empty())
		{
			m_Variants.push_back({ m_ExperimentId, m_DisableDynamicBalance });
		}
		m_VariantIndex = 0;
		m_CurrentVariantId = m_Variants.front().id;
		m_CurrentDisableDynamicBalance =
			m_Variants.front().disableDynamicBalance;

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
			m_CurrentDisableDynamicBalance = variant.disableDynamicBalance;
		}
		else
		{
			m_CurrentVariantId = m_ExperimentId;
			m_CurrentDisableDynamicBalance = m_DisableDynamicBalance;
		}
		result.seed = m_Seeds.empty() ? m_Seed : m_Seeds[m_SeedIndex];
		result.disableDynamicBalance = m_CurrentDisableDynamicBalance;
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
