#include "Game.h"
#include "GameRuntimeState.h"
#include "GameBalanceRuntime.h"
#include "GameBattleState.h"
#include "GamePersistenceRuntime.h"
#include "GameRewardState.h"
#include "RunController.h"
#include "PlayerDeck.h"
#include "RunResultSnapshot.h"
#include "BattleScene.h"
#include "ResultScene.h"
#include "TitleScene.h"
#include "Camera.h"
#include "GameSettingsRuntime.h"

#pragma execution_character_set("utf-8")

#include <iostream>

/// <summary>
/// ゲームの実行状態から現在のシーンを取得する。
/// </summary>
Scene* Game::GetCurrentScene() const
{
	return m_RuntimeState->CurrentScene();
}

/// <summary>
/// ゲームの実行状態から現在のシーン種別を取得する。
/// </summary>
SceneType Game::GetCurrentSceneType() const
{
	return m_RuntimeState->CurrentSceneType();
}

/// <summary>
/// タイトル画面でセーブ済みランを読み込むためのフェード遷移を開始する。
/// </summary>
bool Game::LoadSavedRunWithFade()
{
	if (GetCurrentSceneType() != SceneType::Title) return false;
	m_RuntimeState->Transition().Configure(m_SettingsRuntime->Current().sceneTransition);
	return m_RuntimeState->Transition().Request([this]() { LoadSavedRun(); });
}

/// <summary>
/// 遷移先のシーン種別を検証し、ゲーム状態に応じた切り替えを行う。
/// </summary>
void Game::ChangeScene(SceneType sceneType)
{
	if (sceneType < SceneType::Title || sceneType >= SceneType::Max)
	{
		std::cerr << "[Game] 無効なシーンへの遷移要求を拒否しました" << std::endl;
		return;
	}

	auto& transition = m_RuntimeState->Transition();
	// Debug tools, save restoration and batch runs depend on immediate scene
	// construction. Human scene navigation commits under the black overlay.
	if (!transition.IsCommitting() && GetCurrentScene() && !IsDebugMode() &&
		!m_PersistenceRuntime->IsRestoringRunSave() &&
		!m_BalanceRuntime->autoPlayer.IsEnabled() && !m_BalanceRuntime->validation.IsEnabled())
	{
		const auto* title = dynamic_cast<TitleScene*>(GetCurrentScene());
		transition.Configure(m_SettingsRuntime->Current().sceneTransition);
		transition.Request([this, sceneType]() { ChangeScene(sceneType); }, title && title->IsStartFadeComplete());
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
	// New scenes must already have their final framing during fade-in, before
	// gameplay is allowed its first update.
	m_Camera.SetTarget(DirectX::SimpleMath::Vector3::Zero);
	UpdateSceneCamera();
}
