#include "Game.h"

#pragma execution_character_set("utf-8")

#include "GameSaveManager.h"
#include "UiText.h"

#include <cstdio>

void Game::SetSaveLoadMessage(const std::string& message)
{
	m_SaveLoadMessage = message;
	m_SaveLoadMessageFrames = message.empty() ? 0 : 240;
}

bool Game::HasValidRunSave() const
{
	return GameSaveManager::Inspect().valid;
}

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

bool Game::SaveRunCheckpoint(
	SceneType sceneType,
	bool sceneAlreadyActive,
	bool showNotification)
{
	if (m_BalanceAutoPlayEnabled || m_BalanceValidationEnabled)
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

bool Game::SaveCurrentRun()
{
	SceneType sceneType = SceneType::Max;
	if (dynamic_cast<StageSelectScene*>(m_Scene) != nullptr)
	{
		sceneType = SceneType::Select;
	}
	else if (dynamic_cast<RestSiteScene*>(m_Scene) != nullptr)
	{
		sceneType = SceneType::RestSite;
	}
	else if (dynamic_cast<ShopScene*>(m_Scene) != nullptr)
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

bool Game::LoadSavedRun()
{
	if (dynamic_cast<TitleScene*>(m_Scene) == nullptr)
	{
		SetSaveLoadMessage(UiText::LoadFromTitle);
		return false;
	}
	std::string message;
	const bool loaded = GameSaveManager::Load(*this, message);
	SetSaveLoadMessage(message);
	return loaded;
}
