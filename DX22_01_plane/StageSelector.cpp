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

std::vector<std::string> GetPhaseTestStageIds(int progress)
{
	if (progress <= 2)
	{
		return { "phase_run_p1_mixed6", "phase_run_p1_pairs6" };
	}
	if (progress <= 4)
	{
		return { "phase_run_p3_mixed10", "phase_run_p3_pairs10" };
	}
	return { "phase_prototype_009", "phase_prototype_010" };
}

std::vector<std::string> GetCampaignPhaseStageIds(int floor)
{
	if (floor < 5)
	{
		return {};
	}
	if (floor < 10)
	{
		return {
			"campaign_phase_mid_mixed",
			"campaign_phase_mid_pairs",
		};
	}
	return {
		"campaign_phase_late_mixed",
		"campaign_phase_late_pairs",
	};
}

StageSelector::StageSelector()
    : m_RandomEngine(std::random_device{}())
{
}

const StageData* StageSelector::SelectStage(
    const std::vector<StageData>& stages,
    StageType stageType,
    int progress,
    const std::string& lastStageId,
    const std::vector<std::string>& allowedStageIds)
{
    std::vector<const StageData*> candidates;
    const DifficultyRange range = GetDifficultyRange(progress);

    for (const StageData& stage : stages)
    {
        if (stage.prototypeOnly)
        {
            continue;
        }
        if (stage.stageType != stageType)
        {
            continue;
        }

        if (stageType == StageType::Normal &&
            (stage.difficulty < range.min || stage.difficulty > range.max))
        {
            continue;
        }

		if (stageType == StageType::Normal && !allowedStageIds.empty() &&
			std::find(allowedStageIds.begin(), allowedStageIds.end(), stage.id) ==
				allowedStageIds.end())
		{
			continue;
		}

        candidates.push_back(&stage);
    }

    if (candidates.empty())
    {
        std::cerr << "[StageSelector] 種別「" << ToString(stageType)
            << "」、難易度" << range.min << "～" << range.max
            << "、指定候補に一致するステージがありません。"
            << "未解放ステージへのフォールバックは行いません"
            << std::endl;
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

StageSelectorSnapshot StageSelector::CaptureSnapshot() const
{
    return { m_RandomEngine };
}

void StageSelector::RestoreSnapshot(const StageSelectorSnapshot& snapshot)
{
    m_RandomEngine = snapshot.randomEngine;
}

const StageData* StageSelector::SelectPhaseTestStage(
	const std::vector<StageData>& stages,
	int progress,
	const std::string& lastStageId)
{
	const std::vector<std::string> allowedIds =
		GetPhaseTestStageIds(progress);
	std::vector<const StageData*> candidates;
	for (const StageData& stage : stages)
	{
		if (stage.stageType == StageType::Normal &&
			stage.prototypeOnly &&
			stage.combatMode == CombatMode::PhasePrototype &&
			std::find(allowedIds.begin(), allowedIds.end(), stage.id) !=
				allowedIds.end())
		{
			candidates.push_back(&stage);
		}
	}
	if (candidates.empty())
	{
		std::cerr << "[StageSelector] Phase Test Run progress "
			<< progress << " has no matching stage." << std::endl;
		return nullptr;
	}
	if (candidates.size() > 1 && !lastStageId.empty())
	{
		candidates.erase(
			std::remove_if(candidates.begin(), candidates.end(),
				[&lastStageId](const StageData* stage)
				{
					return stage->id == lastStageId;
				}),
			candidates.end());
	}
	std::uniform_int_distribution<std::size_t> distribution(
		0, candidates.size() - 1);
	return candidates[distribution(m_RandomEngine)];
}

const StageData* StageSelector::SelectCampaignNormalStage(
	const std::vector<StageData>& stages,
	int floor,
	const std::string& lastStageId,
	const std::vector<std::string>& allowedStageIds)
{
	const int normalizedFloor = (std::max)(1, floor);
	const bool phaseBattle = normalizedFloor >= 5;
	const int expectedSecondPhaseCount = normalizedFloor >= 10 ? 6 : 4;
	const DifficultyRange difficulty = GetDifficultyRange(normalizedFloor);
	const std::vector<std::string> campaignPhaseIds =
		GetCampaignPhaseStageIds(normalizedFloor);
	std::vector<const StageData*> candidates;

	for (const StageData& stage : stages)
	{
		if (stage.stageType != StageType::Normal || stage.prototypeOnly)
		{
			continue;
		}
		if (!allowedStageIds.empty() &&
			std::find(allowedStageIds.begin(), allowedStageIds.end(), stage.id) ==
				allowedStageIds.end())
		{
			continue;
		}

		if (!phaseBattle)
		{
			if (stage.combatMode != CombatMode::Legacy ||
				stage.difficulty < difficulty.min ||
				stage.difficulty > difficulty.max ||
				stage.enemies.size() < 3 || stage.enemies.size() > 5)
			{
				continue;
			}
		}
		else
		{
			if (stage.combatMode != CombatMode::PhasePrototype ||
				std::find(campaignPhaseIds.begin(), campaignPhaseIds.end(),
					stage.id) == campaignPhaseIds.end() ||
				stage.phaseBattle.phases.size() != 2 ||
				stage.phaseBattle.phases[0].enemies.size() != 6 ||
				static_cast<int>(
					stage.phaseBattle.phases[1].enemies.size()) !=
					expectedSecondPhaseCount)
			{
				continue;
			}
		}
		candidates.push_back(&stage);
	}

	if (candidates.empty())
	{
		std::cerr << "[StageSelector] Campaign floor " << normalizedFloor
			<< " has no valid "
			<< (phaseBattle ? "phase" : "opening")
			<< " normal stage. No combat-mode fallback was applied."
			<< std::endl;
		return nullptr;
	}
	if (candidates.size() > 1 && !lastStageId.empty())
	{
		candidates.erase(
			std::remove_if(candidates.begin(), candidates.end(),
				[&lastStageId](const StageData* stage)
				{
					return stage->id == lastStageId;
				}),
			candidates.end());
	}
	std::uniform_int_distribution<std::size_t> distribution(
		0, candidates.size() - 1);
	const StageData* selected = candidates[distribution(m_RandomEngine)];
	std::cout << "[StageSelector] Campaign floor " << normalizedFloor
		<< " selected " << selected->id
		<< " / phases " << selected->phaseBattle.phases.size()
		<< std::endl;
	return selected;
}
