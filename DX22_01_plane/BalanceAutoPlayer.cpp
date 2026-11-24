#include "BalanceAutoPlayer.h"
#include "Game.h"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "PlayerBall.h"
#include "StageSelectScene.h"
#include "input.h"
#include "json/json.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>

#pragma execution_character_set("utf-8")

/// <summary>
/// 自動プレイの有効状態を切り替え、判断間隔のカウンターを初期化する。
/// </summary>
void BalanceAutoPlayer::SetEnabled(bool enabled)
{
	m_Enabled = enabled;
	m_DecisionFrame = 0;
}

/// <summary>
/// JSON設定から自動プレイの動作、ランの条件、乱数設定などを読み込む。
/// </summary>
void BalanceAutoPlayer::LoadConfig(
	const std::string& filePath)
{
	std::ifstream file(filePath);
	if (!file)
	{
		std::cout
			<< "[BalanceAutoPlay] Config not found: "
			<< filePath << std::endl;
		return;
	}

	try
	{
		nlohmann::json config;
		file >> config;

		m_Enabled = config.value("enabled", false);
		m_RestartAfterGameOver =
			config.value("restart_after_game_over", true);
		m_StopAfterCurrentRunDefault =
			config.value("stop_after_current_run", false);
		m_StopAfterCurrentRunRequested =
			m_Enabled && m_StopAfterCurrentRunDefault;
		m_DecisionDelayFrames = (std::max)(
			1,
			config.value("decision_delay_frames", 20));
		m_MaximumRuns =
			(std::max)(0, config.value("max_runs", 0));
		m_MinimumShotPower = std::clamp(
			config.value("min_shot_power", 4.0f),
			1.0f,
			8.0f);
		m_MaximumShotPower = std::clamp(
			config.value("max_shot_power", 8.0f),
			m_MinimumShotPower,
			8.0f);
		m_AimJitterDegrees = std::clamp(
			config.value("aim_jitter_degrees", 1.5f),
			0.0f,
			15.0f);

		m_RandomSeed =
			config.value("random_seed", 20260727u);
		m_RandomEngine.seed(m_RandomSeed);

		std::cout
			<< "[BalanceAutoPlay] "
			<< (m_Enabled ? "Enabled" : "Disabled")
			<< " / F8 toggles auto play / F9 stops at run end"
			<< std::endl;
	}
	catch (const nlohmann::json::exception& error)
	{
		std::cerr
			<< "[BalanceAutoPlay] Invalid config: "
			<< error.what() << std::endl;
	}
}

/// <summary>
/// 切替キーと現在のシーンを確認し、自動プレイが有効なら場面に応じた操作を実行する。
/// </summary>
bool BalanceAutoPlayer::Update(Game& game)
{
	if (game.IsDebugMode() || game.IsDebugEditorOpen()) return false;
	if (Input::GetKeyTrigger(VK_F8))
	{
		const bool startAutoPlay = !m_Enabled;
		m_Enabled = startAutoPlay;
		m_StopAfterCurrentRunRequested =
			startAutoPlay && m_StopAfterCurrentRunDefault;
		m_DecisionFrame = 0;

		std::cout
			<< "[BalanceAutoPlay] "
			<< (m_Enabled ? "ON" : "OFF")
			<< std::endl;
	}
	if (m_Enabled && Input::GetKeyTrigger(VK_F9))
	{
		m_StopAfterCurrentRunRequested =
			!m_StopAfterCurrentRunRequested;
		std::cout
			<< "[BalanceAutoPlay] Stop at run end: "
			<< (m_StopAfterCurrentRunRequested ? "ON" : "OFF")
			<< std::endl;
	}

	if (!m_Enabled || game.GetCurrentScene() == nullptr)
	{
		return false;
	}

	auto isDecisionReady = [this]()
	{
		m_DecisionFrame++;
		if (m_DecisionFrame < m_DecisionDelayFrames)
		{
			return false;
		}

		m_DecisionFrame = 0;
		return true;
	};

	if (game.GetCurrentSceneType() == SceneType::Title)
	{
		if (!isDecisionReady())
		{
			return false;
		}

		if (m_MaximumRuns > 0 &&
			m_RunCount >= m_MaximumRuns)
		{
			m_Enabled = false;
			std::cout
				<< "[BalanceAutoPlay] Max runs reached"
				<< std::endl;
			return false;
		}

		game.StartNewRun();
		m_RunCount++;
		game.ChangeScene(SceneType::Select);
		return true;
	}

	if (game.GetCurrentSceneType() == SceneType::Result)
	{
		if (m_StopAfterCurrentRunRequested)
		{
			m_Enabled = false;
			m_StopAfterCurrentRunRequested = false;
			std::cout
				<< "[BalanceAutoPlay] Stopped at terminal result"
				<< std::endl;
			return false;
		}
		if (!m_RestartAfterGameOver)
		{
			m_Enabled = false;
			return false;
		}

		if (!isDecisionReady())
		{
			return false;
		}

		game.ChangeScene(SceneType::Title);
		return true;
	}

	if (StageSelectScene* stageSelect =
		dynamic_cast<StageSelectScene*>(game.GetCurrentScene()))
	{
		if (!isDecisionReady())
		{
			return false;
		}

		ApplyRouteChoice(game, stageSelect);
		return true;
	}

	if (game.GetCurrentSceneType() == SceneType::RestSite)
	{
		if (!isDecisionReady())
		{
			return false;
		}

		ApplyRestSiteAction(game);
		return true;
	}

	if (game.GetCurrentSceneType() == SceneType::Shop)
	{
		if (!isDecisionReady())
		{
			return false;
		}

		ApplyShopAction(game);
		return true;
	}

	if (game.IsClearRewardActive())
	{
		if (!isDecisionReady())
		{
			return false;
		}

		if (game.IsMidBossRelicSelectionActive())
		{
			game.AcquireMidBossRelicOffer(0);
		}
		else if (!game.IsClearRewardChosen())
		{
			ApplyReward(game);
		}
		else
		{
			game.ContinueAfterClearReward();
		}
		return true;
	}

	if (game.GetCurrentSceneType() == SceneType::Battle &&
		game.GetBattleState() == BattleState::AimingDirection)
	{
		if (game.AreAllEnemiesDefeated())
		{
			m_DecisionFrame = 0;
			return false;
		}
		std::vector<PlayerBall*> players =
			GameRuntime::World().GetComponents<PlayerBall>();
		if (players.empty() ||
			players[0] == nullptr ||
			!players[0]->IsIdle() ||
			!game.AreAllBallsStopped())
		{
			m_DecisionFrame = 0;
			return false;
		}

		if (!isDecisionReady())
		{
			return false;
		}

		SelectBall(game);
		return FireShot(game);
	}

	return false;
}
