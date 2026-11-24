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

	/// <summary>
	/// バランス検証が有効かを返す。
	/// </summary>
	bool IsEnabled() const { return m_Enabled; }
	/// <summary>
	/// バランス検証の有効状態を設定する。
	/// </summary>
	void SetEnabled(bool enabled) { m_Enabled = enabled; }
	/// <summary>
	/// 固定したステージ順を使用するかを返す。
	/// </summary>
	bool UsesFixedStageSchedule() const { return m_FixedStageSchedule; }
	/// <summary>
	/// 耐久モードが有効かを返す。
	/// </summary>
	bool IsEnduranceMode() const { return m_EnduranceMode; }
	/// <summary>
	/// 検証と耐久モードが有効であり、上限が通常目標を超える場合に拡張ルートを使用する。
	/// </summary>
	bool UsesExtendedRoute(int normalRouteGoal) const
	{
		return m_Enabled && m_EnduranceMode &&
			m_MaximumClearedStages > normalRouteGoal;
	}
	/// <summary>
	/// 拡張ルートでクリアしたステージ数が検証の上限に達したかを返す。
	/// </summary>
	bool HasReachedMaximumClearedStages(
		int clearedStages,
		int normalRouteAreaGoal) const
	{
		return UsesExtendedRoute(normalRouteAreaGoal) &&
			clearedStages >= m_MaximumClearedStages;
	}

	/// <summary>
	/// 現在の検証ランのシードを返す。
	/// </summary>
	std::uint32_t GetSeed() const { return m_Seed; }
	/// <summary>
	/// 現在のシードの設定一覧での位置を返す。
	/// </summary>
	std::uint32_t GetSeedIndex() const { return m_SeedIndex; }
	/// <summary>
	/// 登録されている検証用シードの数を返す。
	/// </summary>
	std::size_t GetSeedCount() const { return m_Seeds.size(); }
	/// <summary>
	/// 現在の検証条件の設定一覧での位置を返す。
	/// </summary>
	std::uint32_t GetVariantIndex() const { return m_VariantIndex; }
	/// <summary>
	/// 登録済みの検証条件の一覧を返す。
	/// </summary>
	const std::vector<BalanceValidationVariant>& GetVariants() const
	{
		return m_Variants;
	}
	/// <summary>
	/// 現在の検証実験の識別子を返す。
	/// </summary>
	const std::string& GetExperimentId() const { return m_ExperimentId; }
	/// <summary>
	/// 現在選択されている検証条件の識別子を返す。
	/// </summary>
	const std::string& GetCurrentVariantId() const
	{
		return m_CurrentVariantId;
	}
	/// <summary>
	/// 検証ランで許容するクリア済みステージ数の上限を返す。
	/// </summary>
	int GetMaximumClearedStages() const
	{
		return m_MaximumClearedStages;
	}
	bool HasVariant(const std::string& id) const;
	bool UsesAdjustedEarlyStages() const;
	bool UsesAdjustedMidStages() const;
	bool UsesNormalPlayBalance() const;
	/// <summary>
	/// バランスの上書きが有効かを、検証の有効状態とラン固有の強制条件から判定する。
	/// </summary>
	bool HasActiveBalanceOverride() const
	{
		return m_Enabled || m_ForcedVariantForCurrentRun;
	}
	bool UsesTacticalAi() const;
	const std::string& GetTacticalAiProfile() const;
	int GetAttackBudget(int progress) const;
	std::vector<std::string> GetAllowedNormalStageIds(int progress) const;
	const BalanceValidationVariant* GetCurrentVariant() const;

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
		{ "fixed", false, false, false, true, "tactics_v1", -1, -1 },
	};
	bool m_ForcedVariantForCurrentRun = false;
	int m_MaximumClearedStages = 30;
};
