#pragma once

#include <string>
#include <vector>

struct NormalPlayProgressRange final
{
	int minimumProgress = 1;
	int maximumProgress = 1;
	int enemyActionMaximumBudget = -1;
	std::vector<std::string> normalStageIds;
};

// Owns the progress-based combat conditions formally used by normal play.
// Validation variants remain separate so existing A/B runs stay reproducible.
class NormalPlayBalance final
{
public:
	NormalPlayBalance();

	void Load(
		const std::string& filePath =
			"assets/data/normal_play_balance.json");

	int GetEnemyActionMaximumBudget(int progress) const;
	std::vector<std::string> GetAllowedNormalStageIds(int progress) const;
	std::string GetStagePoolId(int progress) const;
	bool IsLoadedFromData() const { return m_LoadedFromData; }

private:
	struct StagePool final
	{
		std::string id;
		int minimumProgress = 1;
		int maximumProgress = 1;
		std::vector<std::string> stageIds;
	};

	void ResetToSafeDefaults();
	const StagePool* FindStagePool(int progress) const;

	bool m_LoadedFromData = false;
	std::vector<NormalPlayProgressRange> m_AttackBudgetRanges;
	std::vector<StagePool> m_StagePools;
};
