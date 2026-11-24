#include "RunProgressController.h"

#include <algorithm>
#include <utility>

// 新規ラン向けに進行状態とルートマップを初期化





/// <summary>
/// 指定のシードでルートを生成し、戦闘回数とフェーズの進行を初期化する。
/// </summary>
void RunProgressController::Reset(std::uint32_t routeSeed)
{
	m_Map.Generate(routeSeed);
	m_ClearedBattles = 0;
	m_AreaProgress = 0;
	m_Phase = RunPhase::NormalRoute;
	m_PhaseNormalBattleEncounters = 0;
}

// セーブデータから検証済みの進行状態を復元
/// <summary>
/// 検証済みのセーブ情報からルートマップとランの進行状態を復元する。
/// </summary>
void RunProgressController::Restore(RunProgressState state)
{
	m_Map = std::move(state.map);
	m_ClearedBattles = (std::max)(0, state.clearedBattles);
	m_AreaProgress = std::clamp(
		state.areaProgress,
		0,
		NormalRouteAreaGoal);
	m_Phase = state.phase;
	m_PhaseNormalBattleEncounters = (std::max)(
		0,
		state.phaseNormalBattleEncounters);
}

// 現在の進行状態をセーブ用の値オブジェクトとして複製
/// <summary>
/// ルートマップ、戦闘回数とフェーズの状態をセーブ用データにまとめる。
/// </summary>
RunProgressState RunProgressController::Capture() const
{
	return {
		m_Map,
		m_ClearedBattles,
		m_AreaProgress,
		m_Phase,
		m_PhaseNormalBattleEncounters,
	};
}

// 現在選択可能なマップノードをアクティブにする。
/// <summary>
/// ルート上で選択可能なノードを進行中にする。
/// </summary>
bool RunProgressController::ChooseNode(int nodeId)
{
	return m_Map.Choose(nodeId);
}

// 進行中のマップノード選択を取り消す。
/// <summary>
/// 進行中のルートノードの選択を取り消す。
/// </summary>
void RunProgressController::CancelActiveNode()
{
	m_Map.CancelActive();
}

// 通常ルートの現在エリアを完了し、必要なら次区間へ進める。
/// <summary>
/// 通常エリアを完了し、進行度と状態に応じて次の区間や準備フェーズに進める。
/// </summary>
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
		const auto choices = m_Map.Available();
		const RunMapNode* preparation = choices.size() == 1
			? m_Map.Node(choices.front()) : nullptr;
		if (preparation == nullptr || preparation->type != StageRouteType::BossPreparation ||
			!m_Map.Choose(preparation->id))
		{
			return AreaCompletionResult::Rejected;
		}
		m_Phase = RunPhase::BossPreparation;
		return AreaCompletionResult::BossPreparationEntered;
	}

	return AreaCompletionResult::Advanced;
}

// 最終準備ノードを完了して最終ボス選択可能状態へ進める。
/// <summary>
/// ボス準備ノードを完了し、最終ボス戦を選択できる状態にする。
/// </summary>
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
/// <summary>
/// 最終ボス戦の開始を確定して戦闘中のフェーズへ移す。
/// </summary>
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
/// <summary>
/// 最終ボス戦の開始を取り消し、選択可能なフェーズに戻す。
/// </summary>
void RunProgressController::CancelFinalBossStart()
{
	if (m_Phase == RunPhase::FinalBoss)
	{
		m_Phase = RunPhase::FinalBossReady;
	}
}

// 最終ボス撃破を記録してラン進行を完了状態へ進める。
/// <summary>
/// 最終ボスノードをクリア済みにしてランの進行を完了する。
/// </summary>
void RunProgressController::CompleteFinalBoss()
{
	m_Map.CompleteActive();
	m_Phase = RunPhase::Completed;
}

// 戦闘ステージのクリア回数を1増やす。
/// <summary>
/// 戦闘をクリアした累計回数を1増やす。
/// </summary>
void RunProgressController::RecordBattleCleared()
{
	++m_ClearedBattles;
}

/// <summary>
/// 現在のフェーズで通常戦に入った回数を1増やす。
/// </summary>
void RunProgressController::RecordPhaseNormalBattleEncounter()
{
	++m_PhaseNormalBattleEncounters;
}
