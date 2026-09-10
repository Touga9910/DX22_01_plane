#pragma once

#include "GameTypes.h"
#include "RunMap.h"

#include <cstdint>

// セーブ復元時に必要なラン進行状態をひとまとまりで受け渡す。
struct RunProgressState
{
	RunMap map;
	int clearedBattles = 0;
	int areaProgress = 0;
	RunPhase phase = RunPhase::NormalRoute;
};

// 通常エリア完了によって発生した進行上の変化を表す。
enum class AreaCompletionResult
{
	Rejected,
	Advanced,
	MapExtended,
	BossPreparationEntered,
};

// ランのマップ、到達度、フェーズの整合性を一元管理する。
class RunProgressController final
{
public:
	static constexpr int NormalRouteAreaGoal = 15;

	// 新規ラン向けに進行状態とルートマップを初期化する。
	void Reset(std::uint32_t routeSeed);

	// セーブデータから検証済みの進行状態を復元する。
	void Restore(RunProgressState state);

	// 現在の進行状態をセーブ用の値オブジェクトとして複製する。
	RunProgressState Capture() const;

	// 現在選択可能なマップノードをアクティブにする。
	bool ChooseNode(int nodeId);

	// 進行中のマップノード選択を取り消す。
	void CancelActiveNode();

	// 通常ルートの現在エリアを完了し、必要なら次区間へ進める。
	AreaCompletionResult CompleteNormalArea(
		bool enduranceMode,
		int maximumClearedStages,
		std::uint32_t extensionSeed);

	// 最終準備ノードを完了して最終ボス選択可能状態へ進める。
	bool CompleteBossPreparation();

	// 最終ボス戦の開始を確定し、戦闘中フェーズへ進める。
	bool BeginFinalBoss();

	// 最終ボス戦開始に失敗した場合に直前のフェーズへ戻す。
	void CancelFinalBossStart();

	// 最終ボス撃破を記録してラン進行を完了状態へ進める。
	void CompleteFinalBoss();

	// 戦闘ステージのクリア回数を1増やす。
	void RecordBattleCleared();

	// 現在のルートマップを読み取り専用で返す。
	const RunMap& GetMap() const { return m_Map; }

	// 現在の戦闘クリア回数を返す。
	int GetClearedBattleCount() const { return m_ClearedBattles; }

	// 現在の通常エリア進行数を返す。
	int GetAreaProgress() const { return m_AreaProgress; }

	// 現在のランフェーズを返す。
	RunPhase GetPhase() const { return m_Phase; }

	// 通常ルートの終了後に最終準備中かを返す。
	bool IsBossPreparation() const
	{
		return m_Phase == RunPhase::BossPreparation;
	}

	// 最終ボスノードを選択可能な状態かを返す。
	bool IsFinalBossReady() const
	{
		return m_Phase == RunPhase::FinalBossReady;
	}

private:
	RunMap m_Map;
	int m_ClearedBattles = 0;
	int m_AreaProgress = 0;
	RunPhase m_Phase = RunPhase::NormalRoute;
};
