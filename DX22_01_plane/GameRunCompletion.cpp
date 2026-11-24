#include "Game.h"
#include "GameBattleState.h"
#include "GamePersistenceRuntime.h"
#include "GameRewardState.h"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "RunController.h"
#include "RunProgressController.h"
#include "PlayerRunStatus.h"
#include "EnemyBall.h"
#include "BalanceLogger.h"
#include "GameSaveManager.h"
#include "json/json.hpp"

#pragma execution_character_set("utf-8")

#include <algorithm>
#include <exception>
#include <utility>

namespace
{
	/// <summary>
	/// まだ撃破されていない有効な敵の数を数える。
	/// </summary>
	int CountAliveEnemies(const std::vector<EnemyBall*>& enemies)
	{
		return static_cast<int>(std::count_if(
			enemies.begin(),
			enemies.end(),
			[](const EnemyBall* enemy)
			{
				return enemy != nullptr && !enemy->IsDefeated();
			}));
	}
}

/// <summary>
/// セーブやロードの結果をユーザーに知らせるメッセージを返す。
/// </summary>
const std::string& Game::GetSaveLoadMessage() const
{
	return m_PersistenceRuntime->SaveLoadMessage();
}

/// <summary>
/// 永続的な進行データを保持するプレイヤープロファイルを返す。
/// </summary>
const ProgressionProfile& Game::GetProgressionProfile() const
{
	return m_PersistenceRuntime->Profile();
}

/// <summary>
/// 直近の進行更新で解放された要素の識別子一覧を返す。
/// </summary>
const std::vector<std::string>& Game::GetLastProgressionUnlocks() const
{
	return m_PersistenceRuntime->LastProgressionUnlocks();
}

/// <summary>
/// 現在のランに適用されているアセンション段階を返す。
/// </summary>
int Game::GetActiveAscension() const
{
	return m_PersistenceRuntime->ActiveAscension();
}

/// <summary>
/// 取得したレリックとランの成績をまとめ、ランの終了結果を確定する。
/// </summary>
void Game::FinalizeRunResult(bool completed)
{
	std::vector<RelicType> acquiredRelics;
	for (const RelicDefinition& relic : RelicCatalog)
	{
		if (HasRelic(relic.type))
		{
			acquiredRelics.push_back(relic.type);
		}
	}
	RunResultSnapshot result = m_PersistenceRuntime->Statistics().CreateSnapshot(
		completed,
		m_RunController->Status().currentHp,
		m_RunController->Status().maxHp,
		acquiredRelics);
	result.clearedBattles =
		m_RunController->Progress().GetClearedBattleCount();
	CompleteRunSession(std::move(result));
	RecordPersistentProgress();
}

/// <summary>
/// ランの結果を永続進行へ反映し、新たな解放項目を記録する。
/// </summary>
void Game::RecordPersistentProgress()
{
	auto& unlocks = m_PersistenceRuntime->LastProgressionUnlocks();
	unlocks.clear();
	if (!m_PersistenceRuntime->PersistentProgressEligible()) return;
	try
	{
		unlocks = m_PersistenceRuntime->Profile().RecordRun(
			GetLastRunResult(),
			m_PersistenceRuntime->ActiveAscension());
		m_PersistenceRuntime->Profile().Save();
	}
	catch (const std::exception& e)
	{
		unlocks.push_back(
			std::string("恒久進行を保存できませんでした：") + e.what());
	}
}

/// <summary>
/// 有効なランのセーブが存在しない場合、次回用のアセンション段階を設定する。
/// </summary>
void Game::SetSelectedAscension(int level)
{
	if (HasValidRunSave()) return;
	auto& profile = m_PersistenceRuntime->Profile();
	profile.selectedAscension = std::clamp(
		level,
		0,
		profile.highestUnlockedAscension);
	try { profile.Save(); } catch (...) {}
}

/// <summary>
/// 最終ボス撃破時にステージ進行と永続結果を確定し、ランを終了する。
/// </summary>
void Game::CompleteFinalBossRun()
{
	// 最終ボス撃破時のラン完了処理と永続結果を確定
	m_RunController->Progress().RecordBattleCleared();
	m_PersistenceRuntime->Statistics().DefeatFinalBoss();
	m_RunController->Progress().CompleteFinalBoss();
	RecordBalanceEvent(
		"final_boss_defeated",
		{
			{ "stage_id", m_RunController->Status().GetSelectedStageId() },
			{ "area_progress", m_RunController->Progress().GetAreaProgress() },
			{ "cleared_battle_count", m_RunController->Progress().GetClearedBattleCount() },
		});
	BalanceLogger::GetInstance().EndRun(
		"completed",
		m_RunController->Status().currentHp,
		m_RunController->Status().maxHp,
		m_RunController->Progress().GetClearedBattleCount());
	FinalizeRunResult(true);
	if (!IsDebugMode()) GameSaveManager::Remove();
	ChangeScene(SceneType::Result);
}

/// <summary>
/// ゲームオーバー時に報酬と使用中の球を処理し、モードに応じてランの終了を確定する。
/// </summary>
void Game::ProcessGameOver()
{
	m_RewardState->Exit();
	if (IsDebugMode() && !IsDebugRunMode()) { FinishDebugBattle(false); return; }
	DiscardCurrentPlayerBall();

	const std::vector<EnemyBall*> enemies =
		GameRuntime::World().GetComponents<EnemyBall>();
	BalanceLogger& logger = BalanceLogger::GetInstance();
	logger.EndShot(
		m_RunController->Status().currentHp,
		CountAliveEnemies(enemies),
		m_BattleState->controller.GetDefeatedEnemyCount());
	logger.EndStage(
		"game_over",
		m_RunController->Status().currentHp,
		m_RunController->Status().maxHp,
		m_BattleState->controller.GetDefeatedEnemyCount());
	logger.EndRun(
		"game_over",
		m_RunController->Status().currentHp,
		m_RunController->Status().maxHp,
		m_RunController->Progress().GetClearedBattleCount());
	FinalizeRunResult(false);
	if (!IsDebugMode()) GameSaveManager::Remove();

	ChangeScene(SceneType::Result);
}
