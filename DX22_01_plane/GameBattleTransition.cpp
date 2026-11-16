#include "Game.h"
#include "GameBalanceRuntime.h"
#include "GameStageOverrideState.h"
#include "RunController.h"
#include "PlayerRunStatus.h"
#include "RunProgressController.h"
#include "StageData.h"
#include "StageDataLoader.h"
#include "StageSelector.h"
#include "json/json.hpp"

#pragma execution_character_set("utf-8")

#include <algorithm>
#include <iostream>

const StageData* Game::GetCurrentStageOverride() const
{
	return m_StageOverrideState->Current();
}

StageType Game::GetScheduledStageType() const
{
	// 現在のランフェーズから次に開始すべき戦闘種別を返す。
	if (m_RunController->Progress().GetPhase() == RunPhase::FinalBossReady ||
		m_RunController->Progress().GetPhase() == RunPhase::FinalBoss)
	{
		return StageType::Boss;
	}
	return StageType::Normal;
}

void Game::StartNextBattle()
{
	StartNextBattle(GetScheduledStageType());
}

void Game::StartNextBattle(StageType stageType)
{
	// 選択された戦闘種別に対応するステージを確定して戦闘へ遷移
	const bool enteringFinalBoss = m_RunController->Progress().BeginFinalBoss();
	if (enteringFinalBoss)
	{
		stageType = StageType::Boss;
	}
	const std::string previousStageId =
		m_RunController->Status().GetSelectedStageId().empty()
		? m_RunController->Status().GetLastStageId()
		: m_RunController->Status().GetSelectedStageId();

	if (StageData* overrideStage =
		m_StageOverrideState->PromoteQueuedToCurrent())
	{
		m_RunController->Status().SetLastStageId(previousStageId);
		m_RunController->Status().SetSelectedStageId(
			overrideStage->id);
		if (enteringFinalBoss)
		{
			overrideStage->stageType = StageType::Boss;
		}
		ChangeScene(SceneType::Battle);
		return;
	}

	m_StageOverrideState->ClearCurrent();
	const std::vector<StageData> stages = StageDataLoader::LoadAll(
		"assets/data/stage_01.json",
		"assets/data/enemy_data.json");

	const bool phaseTestNormalBattle =
		stageType == StageType::Normal &&
		m_RunController->GetCombatMode() == RunCombatMode::PhaseTest;
	const int phaseNormalEncounterOrdinal =
		m_RunController->Progress().GetPhaseNormalBattleEncounterCount() + 1;
	const int phaseSelectionProgress =
		m_RunController->GetPhaseProgressionMode() ==
			PhaseRunProgressionMode::RouteProgress
		? m_RunController->Status().progress
		: phaseNormalEncounterOrdinal;
	const int currentFloor = (std::max)(
		1, m_RunController->Status().progress);
	const std::vector<std::string> allowedNormalStageIds =
		m_BalanceRuntime->GetAllowedNormalStageIds(currentFloor);
	const StageData* selectedStage = nullptr;
	if (phaseTestNormalBattle)
	{
		selectedStage =
			m_RunController->StageSelection().SelectPhaseTestStage(
				stages,
				phaseSelectionProgress,
				previousStageId);
	}
	else if (stageType == StageType::Normal)
	{
		selectedStage =
			m_RunController->StageSelection().SelectCampaignNormalStage(
				stages,
				currentFloor,
				previousStageId,
				allowedNormalStageIds);
	}
	else
	{
		selectedStage = m_RunController->StageSelection().SelectStage(
			stages,
			stageType,
			currentFloor,
			previousStageId,
			allowedNormalStageIds);
	}
	if (selectedStage == nullptr)
	{
		m_RunController->Progress().CancelActiveNode();
		if (enteringFinalBoss)
		{
			m_RunController->Progress().CancelFinalBossStart();
		}
		std::cerr << "[Game] 戦闘ステージを選択できなかったため、"
			"シーン遷移を中止します" << std::endl;
		return;
	}

	m_RunController->Status().SetLastStageId(previousStageId);
	m_RunController->Status().SetSelectedStageId(selectedStage->id);
	if (!phaseTestNormalBattle && stageType == StageType::Normal)
	{
		RecordBalanceEvent(
			"campaign_normal_battle_selected",
			{
				{ "floor", currentFloor },
				{ "stage_id", selectedStage->id },
				{ "combat_mode", ToString(selectedStage->combatMode) },
				{ "phase_count", static_cast<int>(
					selectedStage->phaseBattle.phases.size()) },
			});
	}
	if (phaseTestNormalBattle)
	{
		m_RunController->Progress().RecordPhaseNormalBattleEncounter();
		RecordBalanceEvent(
			"phase_normal_battle_selected",
			{
				{ "encounter_ordinal", phaseNormalEncounterOrdinal },
				{ "selection_progress", phaseSelectionProgress },
				{ "progression_mode", ToString(
					m_RunController->GetPhaseProgressionMode()) },
				{ "stage_id", selectedStage->id },
				{ "phase_count", static_cast<int>(
					selectedStage->phaseBattle.phases.size()) },
			});
	}
	ChangeScene(SceneType::Battle);
}
