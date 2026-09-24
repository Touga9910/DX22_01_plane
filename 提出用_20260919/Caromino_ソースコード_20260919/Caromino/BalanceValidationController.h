#pragma once

#include "GameTypes.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct BalanceValidationRun final
{
	bool enabled = false;
	std::uint32_t seed = 0;
};

// 固定条件の比較実験と、シード／バリアント巡回を所有する。
class BalanceValidationController final
{
public:
	void LoadConfig(
		const std::string& filePath =
			"assets/data/balance_validation.json",
		const std::string& baselineDifficultyProfile = "normal");
	BalanceValidationRun OnRunStarted(
		const std::string& forcedVariant,
		std::optional<std::uint32_t> forcedSeed);

	bool IsEnabled() const { return m_Enabled; }
	void SetEnabled(bool enabled) { m_Enabled = enabled; }
	bool UsesFixedStageSchedule() const { return m_FixedStageSchedule; }
	bool IsEnduranceMode() const { return m_EnduranceMode; }
	bool UsesExtendedRoute(int normalRouteGoal) const
	{
		return m_Enabled && m_EnduranceMode &&
			m_MaximumClearedStages > normalRouteGoal;
	}
	bool HasReachedMaximumClearedStages(
		int clearedStages,
		int normalRouteAreaGoal) const
	{
		return UsesExtendedRoute(normalRouteAreaGoal) &&
			clearedStages >= m_MaximumClearedStages;
	}

	std::uint32_t GetSeed() const { return m_Seed; }
	std::uint32_t GetSeedIndex() const { return m_SeedIndex; }
	std::size_t GetSeedCount() const { return m_Seeds.size(); }
	std::uint32_t GetVariantIndex() const { return m_VariantIndex; }
	const std::vector<BalanceValidationVariant>& GetVariants() const
	{
		return m_Variants;
	}
	const std::string& GetExperimentId() const { return m_ExperimentId; }
	const std::string& GetCurrentVariantId() const
	{
		return m_CurrentVariantId;
	}
	int GetMaximumClearedStages() const
	{
		return m_MaximumClearedStages;
	}
	bool HasVariant(const std::string& id) const;

private:
	bool m_Enabled = false;
	bool m_FixedStageSchedule = true;
	bool m_EnduranceMode = false;
	std::uint32_t m_Seed = 20260807u;
	std::vector<std::uint32_t> m_Seeds{ 20260807u };
	std::uint32_t m_RunCounter = 0;
	std::uint32_t m_SeedIndex = 0;
	std::uint32_t m_VariantIndex = 0;
	std::string m_ExperimentId = "fixed_baseline";
	std::string m_CurrentVariantId = "fixed";
	std::vector<BalanceValidationVariant> m_Variants{
		{ "fixed" },
	};
	int m_MaximumClearedStages = 30;
};
