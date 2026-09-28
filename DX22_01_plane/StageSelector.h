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

class StageSelector
{
public:
    StageSelector();
	void Seed(std::uint32_t seed) { m_RandomEngine.seed(seed); }

    const StageData* SelectStage(
        const std::vector<StageData>& stages,
        StageType stageType,
        int progress,
        const std::string& lastStageId);

    StageSelectorSnapshot CaptureSnapshot() const;
    void RestoreSnapshot(const StageSelectorSnapshot& snapshot);

private:
    std::mt19937 m_RandomEngine;
};
