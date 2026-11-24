#include "StageSelector.h"

#include <algorithm>
#include <iostream>

/// <summary>
/// 進行度に対応するステージ難易度の範囲を返す。
/// </summary>
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

/// <summary>
/// 進行度に対応するフェーズ検証用ステージの識別子を返す。
/// </summary>
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

/// <summary>
/// キャンペーンの階層に対応する通常戦ステージの候補を返す。
/// </summary>
std::vector<std::string> GetCampaignPhaseStageIds(int floor)
{
	if (floor < 5)
	{
		return floor < 3
			? std::vector<std::string>{ "normal_001", "normal_002" }
			: std::vector<std::string>{ "normal_003", "board_impact_brittle_wall" };
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

/// <summary>
/// ステージ選択用の乱数エンジンを不定のシード値で初期化する。
/// </summary>
StageSelector::StageSelector()
    : m_RandomEngine(std::random_device{}())
{
}

/// <summary>
/// 種別や難易度、許可リストから候補を絞り、ステージを抽選する。
/// </summary>
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

/// <summary>
/// ステージ抽選の再開に必要な乱数エンジンの状態を取得する。
/// </summary>
StageSelectorSnapshot StageSelector::CaptureSnapshot() const
{
    return { m_RandomEngine };
}

/// <summary>
/// 保存された状態からステージ抽選の乱数エンジンを復元する。
/// </summary>
void StageSelector::RestoreSnapshot(const StageSelectorSnapshot& snapshot)
{
    m_RandomEngine = snapshot.randomEngine;
}

/// <summary>
/// 進行度に対応するフェーズ検証用の通常ステージを抽選する。
/// </summary>
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

/// <summary>
/// 階層と許可リストに合う複数フェーズの通常戦ステージを抽選する。
/// </summary>
const StageData* StageSelector::SelectCampaignNormalStage(
	const std::vector<StageData>& stages,
	int floor,
	const std::string& lastStageId,
	const std::vector<std::string>& allowedStageIds)
{
	const int normalizedFloor = (std::max)(1, floor);
	const std::vector<std::string> campaignPhaseIds =
		GetCampaignPhaseStageIds(normalizedFloor);
	const auto& candidatesForFloor = allowedStageIds.empty()
		? campaignPhaseIds : allowedStageIds;
	std::vector<const StageData*> candidates;

	for (const StageData& stage : stages)
	{
		if (stage.stageType != StageType::Normal || stage.prototypeOnly)
		{
			continue;
		}
		if (stage.combatMode != CombatMode::PhasePrototype ||
			stage.phaseBattle.phases.size() < 2 ||
			std::find(candidatesForFloor.begin(), candidatesForFloor.end(),
				stage.id) == candidatesForFloor.end())
		{
			continue;
		}
		candidates.push_back(&stage);
	}

	if (candidates.empty())
	{
		std::cerr << "[StageSelector] Campaign floor " << normalizedFloor
			<< " has no valid phase normal stage."
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
