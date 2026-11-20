#include "Game.h"
#include "GamePauseState.h"
#include "GameDebugController.h"
#include "BattleScene.h"
#include "StageSelectScene.h"
#include "RestSiteScene.h"
#include "ShopScene.h"
#include "TitleScene.h"

#pragma execution_character_set("utf-8")

void Game::RequestPauseToggle()
{
	m_PauseState->RequestPauseToggle();
}

void Game::RequestManualSave()
{
	m_PauseState->RequestManualSave();
}

void Game::RequestFullscreenToggle()
{
	m_PauseState->RequestFullscreenToggle();
}

bool Game::IsPaused() const
{
	return m_PauseState->IsPaused();
}

void Game::SetPaused(bool paused)
{
	m_PauseState->SetPaused(paused);
}

void Game::ResumeAndDismissPause()
{
	m_PauseState->ResumeAndDismissConfirmation();
}

bool Game::CanPause() const
{
	const auto* title=dynamic_cast<TitleScene*>(GetCurrentScene());
    return (title && title->AllowsPause()) || IsRunSessionActive() &&
		(dynamic_cast<BattleScene*>(GetCurrentScene()) != nullptr ||
		 dynamic_cast<StageSelectScene*>(GetCurrentScene()) != nullptr ||
		 dynamic_cast<RestSiteScene*>(GetCurrentScene()) != nullptr ||
		 dynamic_cast<ShopScene*>(GetCurrentScene()) != nullptr);
}

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
