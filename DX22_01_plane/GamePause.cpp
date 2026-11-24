#include "Game.h"
#include "GamePauseState.h"
#include "GameDebugController.h"
#include "BattleScene.h"
#include "StageSelectScene.h"
#include "RestSiteScene.h"
#include "ShopScene.h"
#include "TitleScene.h"

#pragma execution_character_set("utf-8")

/// <summary>
/// 次の更新でポーズ状態を切り替えるように要求する。
/// </summary>
void Game::RequestPauseToggle()
{
	m_PauseState->RequestPauseToggle();
}

/// <summary>
/// 次の更新でランを手動保存するように要求する。
/// </summary>
void Game::RequestManualSave()
{
	m_PauseState->RequestManualSave();
}

/// <summary>
/// 次の更新で全画面表示を切り替えるように要求する。
/// </summary>
void Game::RequestFullscreenToggle()
{
	m_PauseState->RequestFullscreenToggle();
}

/// <summary>
/// ゲームが現在ポーズ中かを返す。
/// </summary>
bool Game::IsPaused() const
{
	return m_PauseState->IsPaused();
}

/// <summary>
/// ゲームのポーズ状態を指定値に設定する。
/// </summary>
void Game::SetPaused(bool paused)
{
	m_PauseState->SetPaused(paused);
}

/// <summary>
/// ゲームを再開し、タイトルへ戻る確認表示を閉じる。
/// </summary>
void Game::ResumeAndDismissPause()
{
	m_PauseState->ResumeAndDismissConfirmation();
}

/// <summary>
/// 現在のシーンとランの状態から、ゲームをポーズできるかを判定する。
/// </summary>
bool Game::CanPause() const
{
	const auto* title=dynamic_cast<TitleScene*>(GetCurrentScene());
    return (title && title->AllowsPause()) || IsRunSessionActive() &&
		(dynamic_cast<BattleScene*>(GetCurrentScene()) != nullptr ||
		 dynamic_cast<StageSelectScene*>(GetCurrentScene()) != nullptr ||
		 dynamic_cast<RestSiteScene*>(GetCurrentScene()) != nullptr ||
		 dynamic_cast<ShopScene*>(GetCurrentScene()) != nullptr);
}

/// <summary>
/// 現在のランを再開可能な状態で保存し、成功したらタイトル画面に戻る。
/// </summary>
bool Game::SaveAndReturnToTitle()
{
	if (IsDebugMode()) { m_DebugController->RequestExit(); return true; }
	if (IsClearRewardActive() && !IsClearRewardChosen())
	{
		SetSaveLoadMessage(
			"報酬を選択して次のルートへ進んでから保存してください。");
		return false;
	}

	CaptureCurrentPlayerStatus();
	SceneType resumeScene = SceneType::Max;
	bool sceneAlreadyActive = true;
	if (dynamic_cast<BattleScene*>(GetCurrentScene()) != nullptr)
	{
		resumeScene = IsClearRewardActive()
			? SceneType::Select
			: SceneType::Battle;
		sceneAlreadyActive = false;
	}
	else if (dynamic_cast<StageSelectScene*>(GetCurrentScene()) != nullptr)
	{
		resumeScene = SceneType::Select;
	}
	else if (dynamic_cast<RestSiteScene*>(GetCurrentScene()) != nullptr)
	{
		resumeScene = SceneType::RestSite;
	}
	else if (dynamic_cast<ShopScene*>(GetCurrentScene()) != nullptr)
	{
		resumeScene = SceneType::Shop;
	}

	if (resumeScene == SceneType::Max ||
		!SaveRunCheckpoint(resumeScene, sceneAlreadyActive, true))
	{
		return false;
	}

	ResumeAndDismissPause();
	DeactivateRunSession();
	ChangeScene(SceneType::Title);
	return true;
}
