#include "Game.h"

#pragma execution_character_set("utf-8")

#include "GameSaveManager.h"
#include "RestSiteScene.h"
#include "ShopScene.h"
#include "StageSelectScene.h"
#include "TitleScene.h"
#include "UiText.h"

#include <cstdio>

// Save Load Messageを設定する。
void Game::SetSaveLoadMessage(const std::string& message)
{
	m_SaveLoadMessage = message;
	m_SaveLoadMessageFrames = message.empty() ? 0 : 240;
}

// Valid Run Saveを保持しているか判定する。
bool Game::HasValidRunSave() const
{
	return GameSaveManager::Inspect().valid;
}

// Run Save Summaryを取得する。
std::string Game::GetRunSaveSummary() const
{
	const RunSaveInfo info = GameSaveManager::Inspect();
	if (!info.exists)
	{
		return UiText::NoSave;
	}
	if (!info.valid)
	{
		return UiText::SaveValidationFailed;
	}
	char summary[256]{};
	sprintf_s(
		summary,
		UiText::SaveSummaryFormat,
		info.floor,
		info.currentHp,
		info.maxHp,
		info.money);
	return summary;
}

// Run Checkpointを保存する。
bool Game::SaveRunCheckpoint(
	SceneType sceneType,
	bool sceneAlreadyActive,
	bool showNotification)
{
	if (m_DebugMode || m_BalanceAutoPlayEnabled || m_BalanceValidationEnabled)
	{
		return false;
	}
	std::string message;
	const bool saved = GameSaveManager::Save(
		*this,
		sceneType,
		sceneAlreadyActive,
		message);
	if (showNotification || !saved)
	{
		SetSaveLoadMessage(message);
	}
	return saved;
}

// Current Runを保存する。
bool Game::SaveCurrentRun()
{
	if (m_DebugMode) { SetSaveLoadMessage("デバッグ条件は専用画面の「条件を保存」で保存できます。"); return false; }
	SceneType sceneType = SceneType::Max;
	if (dynamic_cast<StageSelectScene*>(m_SceneManager.Get()) != nullptr)
	{
		sceneType = SceneType::Select;
	}
	else if (dynamic_cast<RestSiteScene*>(m_SceneManager.Get()) != nullptr)
	{
		sceneType = SceneType::RestSite;
	}
	else if (dynamic_cast<ShopScene*>(m_SceneManager.Get()) != nullptr)
	{
		sceneType = SceneType::Shop;
	}
	else
	{
		SetSaveLoadMessage(UiText::BattleAutosaveNotice);
		return false;
	}
	return SaveRunCheckpoint(sceneType, true, true);
}

// Saved Runを読み込む。
bool Game::LoadSavedRun()
{
	if (dynamic_cast<TitleScene*>(m_SceneManager.Get()) == nullptr)
	{
		SetSaveLoadMessage(UiText::LoadFromTitle);
		return false;
	}
	std::string message;
	const bool loaded = GameSaveManager::Load(*this, message);
	SetSaveLoadMessage(message);
	return loaded;
}
