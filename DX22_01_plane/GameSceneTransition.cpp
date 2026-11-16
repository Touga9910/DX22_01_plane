#include "Game.h"
#include "GameRuntimeState.h"
#include "GameBattleState.h"
#include "GamePersistenceRuntime.h"
#include "GameRewardState.h"
#include "RunController.h"
#include "PlayerDeck.h"
#include "RunResultSnapshot.h"
#include "BattleScene.h"
#include "ResultScene.h"

#pragma execution_character_set("utf-8")

#include <iostream>

Scene* Game::GetCurrentScene() const
{
	return m_RuntimeState->CurrentScene();
}

SceneType Game::GetCurrentSceneType() const
{
	return m_RuntimeState->CurrentSceneType();
}

void Game::ChangeScene(SceneType sceneType)
{
	if (sceneType < SceneType::Title || sceneType >= SceneType::Max)
	{
		std::cerr << "[Game] 無効なシーンへの遷移要求を拒否しました" << std::endl;
		return;
	}

	ResetFrameTiming();
	int score = 0;
	Scene* currentScene = m_Instance->GetCurrentScene();

	if (currentScene != nullptr)
	{
		m_Instance->CaptureCurrentPlayerStatus();
		if (!m_Instance->m_PersistenceRuntime->IsRestoringRunSave() &&
			(sceneType == SceneType::Select ||
			 sceneType == SceneType::Battle ||
			 sceneType == SceneType::RestSite ||
			 sceneType == SceneType::Shop))
		{
			m_Instance->SaveRunCheckpoint(sceneType, false, false);
		}

		if (BattleScene* battleScene =
			dynamic_cast<BattleScene*>(currentScene))
		{
			score = battleScene->GetScore();
		}
	}

	// =====================================
	// 新しいステージへ入るときに報酬取得状態をリセット
	// =====================================
	if (sceneType == SceneType::Battle)
	{
		m_RewardState->Deactivate();
		m_LastBattleResult = BattleResult::None;

		m_BattleState->controller.StartBattle();

		m_RunController->Deck().Reset();

		if (!BeginBallSelection())
		{
			m_BattleState->controller.NotifyPlayerDefeated();
		}

		m_RunController->BeginStageReward();
		m_RewardState->ClearMessage();
	}
	else
	{
		m_RewardState->Deactivate();
		m_BattleState->controller.Reset();
	}

	if (sceneType == SceneType::Title && IsDebugMode())
	{
		EndDebugMode();
	}

	if (!m_Instance->m_RuntimeState->ChangeScene(sceneType))
	{
		if (sceneType == SceneType::Battle)
		{
			m_BattleState->controller.Reset();
		}
		std::cerr << "[Game] シーンの生成に失敗しました" << std::endl;
		return;
	}

	if (sceneType == SceneType::Result)
	{
		if (ResultScene* resultScene = dynamic_cast<ResultScene*>(
			m_Instance->GetCurrentScene()))
		{
			resultScene->SetScore(score);
		}
	}

	m_Instance->InvalidateDebugCombatForecast("シーン変更");
}
