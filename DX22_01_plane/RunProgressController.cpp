#include "RunProgressController.h"

#include <algorithm>
#include <utility>

// 新規ラン向けに進行状態とルートマップを初期化する。
void RunProgressController::Reset(std::uint32_t routeSeed)
{
	m_Map.Generate(routeSeed);
	m_ClearedBattles = 0;
	m_AreaProgress = 0;
	m_Phase = RunPhase::NormalRoute;
}

// セーブデータから検証済みの進行状態を復元する。
void RunProgressController::Restore(RunProgressState state)
{
	m_Map = std::move(state.map);
	m_ClearedBattles = (std::max)(0, state.clearedBattles);
	m_AreaProgress = std::clamp(
		state.areaProgress,
		0,
		NormalRouteAreaGoal);
	m_Phase = state.phase;
}

// 現在の進行状態をセーブ用の値オブジェクトとして複製する。
RunProgressState RunProgressController::Capture() const
{
	return {
		m_Map,
		m_ClearedBattles,
		m_AreaProgress,
		m_Phase,
	};
}

// 現在選択可能なマップノードをアクティブにする。
bool RunProgressController::ChooseNode(int nodeId)
{
	return m_Map.Choose(nodeId);
}

// 進行中のマップノード選択を取り消す。
void RunProgressController::CancelActiveNode()
{
	m_Map.CancelActive();
}

// 通常ルートの現在エリアを完了し、必要なら次区間へ進める。
AreaCompletionResult RunProgressController::CompleteNormalArea(
	bool enduranceMode,
	int maximumClearedStages,
	std::uint32_t extensionSeed)
{
	if (m_Phase != RunPhase::NormalRoute || !m_Map.CompleteActive())
	{
		return AreaCompletionResult::Rejected;
	}

	++m_AreaProgress;
	if (enduranceMode &&
		maximumClearedStages > NormalRouteAreaGoal &&
		m_AreaProgress >= m_Map.StartArea() + m_Map.AreaCount())
	{
		m_Map.Generate(extensionSeed, m_AreaProgress);
		return AreaCompletionResult::MapExtended;
	}

	if (!enduranceMode && m_AreaProgress >= NormalRouteAreaGoal)
	{
		m_AreaProgress = NormalRouteAreaGoal;
		m_Phase = RunPhase::BossPreparation;
		if (!m_Map.Choose(m_Map.AreaCount() * 3))
		{
			m_Phase = RunPhase::NormalRoute;
			return AreaCompletionResult::Rejected;
		}
		return AreaCompletionResult::BossPreparationEntered;
	}

	return AreaCompletionResult::Advanced;
}

// 最終準備ノードを完了して最終ボス選択可能状態へ進める。
bool RunProgressController::CompleteBossPreparation()
{
	if (m_Phase != RunPhase::BossPreparation || !m_Map.CompleteActive())
	{
		return false;
	}
	m_Phase = RunPhase::FinalBossReady;
	return true;
}

// 最終ボス戦の開始を確定し、戦闘中フェーズへ進める。
bool RunProgressController::BeginFinalBoss()
{
	if (m_Phase != RunPhase::FinalBossReady)
	{
		return false;
	}
	m_Phase = RunPhase::FinalBoss;
	return true;
}

// 最終ボス戦開始に失敗した場合に直前のフェーズへ戻す。
void RunProgressController::CancelFinalBossStart()
{
	if (m_Phase == RunPhase::FinalBoss)
	{
		m_Phase = RunPhase::FinalBossReady;
	}
}

// 最終ボス撃破を記録してラン進行を完了状態へ進める。
void RunProgressController::CompleteFinalBoss()
{
	m_Map.CompleteActive();
	m_Phase = RunPhase::Completed;
}

// 戦闘ステージのクリア回数を1増やす。
void RunProgressController::RecordBattleCleared()
{
	++m_ClearedBattles;
}
