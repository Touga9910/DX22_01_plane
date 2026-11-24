#include "Game.h"
#include "GameBalanceRuntime.h"
#include "GamePersistenceRuntime.h"
#include "GameRewardState.h"
#include "RunController.h"
#include "PlayerRunStatus.h"
#include "RunProgressController.h"
#include "RunProgressConstants.h"
#include "RunMap.h"
#include "json/json.hpp"

#pragma execution_character_set("utf-8")

#include <algorithm>

/// <summary>
/// 通常エリアの完了を進行状態に反映し、完了やボス準備開始のイベントを記録する。
/// </summary>
void Game::CompleteNormalRouteArea(const char* areaType)
{
	// 現在の通常エリアを完了し、ラン進行と関連ログを同期
	const bool longValidationRun =
		m_BalanceRuntime->validation.UsesExtendedRoute(
			kNormalRouteAreaGoal);
	const AreaCompletionResult completion =
		m_RunController->Progress().CompleteNormalArea(
			longValidationRun,
			m_BalanceRuntime->validation.GetMaximumClearedStages(),
			m_BalanceRuntime->routeSelectionSeed + static_cast<std::uint32_t>(
				m_RunController->Progress().GetAreaProgress() + 1));
	if (completion == AreaCompletionResult::Rejected)
	{
		return;
	}

	const int areaProgress = m_RunController->Progress().GetAreaProgress();
	m_RunController->Status().progress = (std::min)(
		kNormalRouteAreaGoal,
		areaProgress + 1);
	m_PersistenceRuntime->Statistics().CompleteArea(areaProgress);
	RecordBalanceEvent(
		"route_area_completed",
		{
			{ "map_path", m_RunController->Progress().GetMap().Path() },
			{ "area_type", areaType != nullptr ? areaType : "unknown" },
			{ "area_progress", areaProgress },
			{ "area_goal", kNormalRouteAreaGoal },
		});

	if (completion == AreaCompletionResult::MapExtended)
	{
		RecordBalanceEvent(
			"run_map_extended",
			m_RunController->Progress().GetMap().Snapshot());
	}
	if (completion == AreaCompletionResult::BossPreparationEntered)
	{
		m_RunController->Status().progress = kNormalRouteAreaGoal;
		RecordBalanceEvent(
			"boss_preparation_entered",
			{
				{ "area_progress", areaProgress },
			});
	}
}

/// <summary>
/// ランの進行段階に応じて、ボス戦前の休憩所か次の進路選択に進む。
/// </summary>
void Game::EnterNextRouteAfterArea()
{
	// 現在のランフェーズに応じて次のルート選択先へ遷移
	if (m_RunController->Progress().IsBossPreparation())
	{
		ChangeScene(SceneType::RestSite);
	}
	else
	{
		ChangeScene(SceneType::Select);
	}
}

/// <summary>
/// ショップの利用完了を記録し、次の進路へ遷移する。
/// </summary>
void Game::LeaveShop()
{
	CompleteNormalRouteArea("shop");
	EnterNextRouteAfterArea();
}

/// <summary>
/// 通常の休憩所やボス戦前の準備を完了し、次の進行先へ遷移する。
/// </summary>
void Game::LeaveRestSite()
{
	// 休憩所の利用完了を記録して次のルートへ進める。
	if (m_RunController->Progress().IsBossPreparation())
	{
		if (!m_RunController->Progress().CompleteBossPreparation())
		{
			return;
		}
		RecordBalanceEvent(
			"boss_preparation_completed",
			{
				{ "area_progress", m_RunController->Progress().GetAreaProgress() },
				{ "next_phase", ToString(m_RunController->Progress().GetPhase()) },
			});
		ChangeScene(SceneType::Select);
		return;
	}

	CompleteNormalRouteArea("rest_site");
	EnterNextRouteAfterArea();
}

/// <summary>
/// クリア報酬の選択を終了し、現在の進行段階に応じた進路へ移る。
/// </summary>
void Game::ContinueAfterClearReward()
{
	m_RewardState->Exit();
	EnterNextRouteAfterArea();
}
