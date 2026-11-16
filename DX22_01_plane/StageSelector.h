#pragma once

#include <random>
#include <cstdint>
#include <string>
#include <vector>
#include "StageData.h"

struct DifficultyRange
{
    int min = 1;
    int max = 1;
};

struct StageSelectorSnapshot
{
    std::mt19937 randomEngine;
};

DifficultyRange GetDifficultyRange(int progress);
std::vector<std::string> GetPhaseTestStageIds(int progress);
std::vector<std::string> GetCampaignPhaseStageIds(int floor);

class StageSelector
{
public:
    StageSelector();
	void Seed(std::uint32_t seed) { m_RandomEngine.seed(seed); }

    const StageData* SelectStage(
        const std::vector<StageData>& stages,
        StageType stageType,
        int progress,
        const std::string& lastStageId,
        const std::vector<std::string>& allowedStageIds = {});

	const StageData* SelectPhaseTestStage(
		const std::vector<StageData>& stages,
		int progress,
		const std::string& lastStageId);

	const StageData* SelectCampaignNormalStage(
		const std::vector<StageData>& stages,
		int floor,
		const std::string& lastStageId,
		const std::vector<std::string>& allowedStageIds);

    StageSelectorSnapshot CaptureSnapshot() const;
    void RestoreSnapshot(const StageSelectorSnapshot& snapshot);

private:
    std::mt19937 m_RandomEngine;
};
