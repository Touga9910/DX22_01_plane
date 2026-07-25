#include "StageSelector.h"

#include <algorithm>
#include <iostream>

DifficultyRange GetDifficultyRange(int progress)
{
    if (progress <= 2)
    {
        return { 1, 1 };
    }
    if (progress <= 4)
    {
        return { 1, 2 };
    }

    return { 2, 3 };
}

StageSelector::StageSelector()
    : m_RandomEngine(std::random_device{}())
{
}

const StageData* StageSelector::SelectStage(
    const std::vector<StageData>& stages,
    StageType stageType,
    int progress,
    const std::string& lastStageId)
{
    std::vector<const StageData*> candidates;
    const DifficultyRange range = GetDifficultyRange(progress);

    for (const StageData& stage : stages)
    {
        if (stage.stageType != stageType)
        {
            continue;
        }

        if (stageType == StageType::Normal &&
            (stage.difficulty < range.min || stage.difficulty > range.max))
        {
            continue;
        }

        candidates.push_back(&stage);
    }

    if (candidates.empty())
    {
        std::cerr << "[StageSelector] 種別「" << ToString(stageType)
            << "」、難易度" << range.min << "～" << range.max
            << "に一致するステージがないため、難易度を無視します"
            << std::endl;

        for (const StageData& stage : stages)
        {
            if (stage.stageType == stageType)
            {
                candidates.push_back(&stage);
            }
        }
    }

    if (candidates.empty())
    {
        std::cerr << "[StageSelector] 種別「" << ToString(stageType)
            << "」のステージが存在しません" << std::endl;
        return nullptr;
    }

    if (candidates.size() > 1 && !lastStageId.empty())
    {
        candidates.erase(
            std::remove_if(
                candidates.begin(),
                candidates.end(),
                [&lastStageId](const StageData* stage)
                {
                    return stage->id == lastStageId;
                }),
            candidates.end());
    }

    std::uniform_int_distribution<size_t> distribution(
        0,
        candidates.size() - 1);
    const StageData* selected = candidates[distribution(m_RandomEngine)];

    std::cout << "[StageSelector] Selected Stage ID: " << selected->id
        << " / Type: " << ToString(selected->stageType)
        << " / Difficulty: " << selected->difficulty
        << " / Progress: " << progress
        << " / Enemy Count: " << selected->enemies.size()
        << std::endl;

    return selected;
}
