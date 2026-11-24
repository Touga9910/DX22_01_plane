#include "Game.h"

#pragma execution_character_set("utf-8")

#include "GameBalanceRuntime.h"
#include "GamePersistenceRuntime.h"
#include "GameSaveManager.h"
#include "GameRunSaveState.h"
#include "RestSiteScene.h"
#include "UiText.h"

#include <cstdio>
#include <exception>
#include <utility>

/// <summary>
/// セーブやロードの結果を知らせるメッセージを保存する。
/// </summary>
void Game::SetSaveLoadMessage(const std::string& message)
{
	m_PersistenceRuntime->SetSaveLoadMessage(message);
}

/// <summary>
/// 再開に使用できる有効なランのセーブが存在するかを返す。
/// </summary>
bool Game::HasValidRunSave() const
{
	return GameSaveManager::Inspect().valid;
}

/// <summary>
/// ファイルの有無と検証結果をもとに、再開データの概要を表示用文字列にまとめる。
/// </summary>
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

/// <summary>
/// 現在のランを指定した再開先でチェックポイントに保存し、必要に応じて結果を通知する。
/// </summary>
bool Game::SaveRunCheckpoint(
	SceneType sceneType,
	bool sceneAlreadyActive,
	bool showNotification)
{
	if (IsDebugMode())
	{
		if (showNotification)
		{
			SetSaveLoadMessage(
				"デバッグ中は通常のランセーブを使用できません。");
		}
		return false;
	}
	if (m_BalanceRuntime->autoPlayer.IsEnabled())
	{
		if (showNotification)
		{
			SetSaveLoadMessage(
				"自動プレイ中は通常のランセーブを使用できません。");
		}
		return false;
	}
	if (m_BalanceRuntime->validation.IsEnabled())
	{
		if (showNotification)
		{
			SetSaveLoadMessage(
				"バランス検証中は通常のランセーブを使用できません。");
		}
		return false;
	}
	std::string message;
	bool restActionUsed = false;
	if (sceneAlreadyActive && sceneType == SceneType::RestSite)
	{
		const RestSiteScene* rest =
			dynamic_cast<const RestSiteScene*>(GetCurrentScene());
		restActionUsed = rest != nullptr && rest->HasUsedAction();
	}
	const GameRunSaveSnapshot snapshot = CaptureRunSaveSnapshot();
	const bool saved = GameSaveManager::Save(
		snapshot,
		sceneType,
		IsDebugMode(),
		restActionUsed,
		message);
	if (showNotification || !saved)
	{
		SetSaveLoadMessage(message);
	}
	return saved;
}

/// <summary>
/// 現在のシーンと状態から再開先を決め、ランをセーブする。
/// </summary>
bool Game::SaveCurrentRun()
{
	if (IsDebugMode()) { SetSaveLoadMessage("デバッグ条件は専用画面の「条件を保存」で保存できます。"); return false; }
	SceneType sceneType = SceneType::Max;
	if (GetCurrentSceneType() == SceneType::Select)
	{
		sceneType = SceneType::Select;
	}
	else if (GetCurrentSceneType() == SceneType::RestSite)
	{
		sceneType = SceneType::RestSite;
	}
	else if (GetCurrentSceneType() == SceneType::Shop)
	{
		sceneType = SceneType::Shop;
	}
	else if (GetCurrentSceneType() == SceneType::Event)
	{
		sceneType = SceneType::Event;
	}
	else
	{
		SetSaveLoadMessage(UiText::BattleAutosaveNotice);
		return false;
	}
	return SaveRunCheckpoint(sceneType, true, true);
}

/// <summary>
/// タイトル画面から有効なセーブを読み込み、ランを再開状態に復元する。
/// </summary>
bool Game::LoadSavedRun()
{
	if (GetCurrentSceneType() != SceneType::Title)
	{
		SetSaveLoadMessage(UiText::LoadFromTitle);
		return false;
	}
	std::string message;
	GameRunRestoreRequest request{};
	const GameRunLoadRules rules{
		IsEnduranceMode(),
		GetNormalRouteAreaGoal(),
		GetRelicCount(),
		GetShopBallCount(),
	};
	bool loaded = GameSaveManager::Load(rules, request, message);
	if (loaded)
	{
		try
		{
			RestoreRunSaveSnapshot(std::move(request));
		}
		catch (const std::exception& exception)
		{
			message = std::string(UiText::LoadFailedPrefix) + exception.what();
			loaded = false;
		}
	}
	SetSaveLoadMessage(message);
	return loaded;
}
