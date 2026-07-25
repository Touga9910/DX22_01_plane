#pragma once

#include <random>
#include <string>
#include <vector>
#include "StageData.h"

struct DifficultyRange
{
    int min = 1;
    int max = 1;
};

DifficultyRange GetDifficultyRange(int progress);

class StageSelector
{
public:
    StageSelector();

    const StageData* SelectStage(
        const std::vector<StageData>& stages,
        StageType stageType,
        int progress,
        const std::string& lastStageId);

private:
    std::mt19937 m_RandomEngine;
};
