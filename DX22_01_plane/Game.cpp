#include "Game.h"
#include "GameBattleState.h"
#include "IllustratedUi.h"
#include "Application.h"
#include "GameBalanceRuntime.h"
#include "GameObject.h"
#include "GameRuntime.h"
#include "GameRuntimeState.h"
#include "GameWorld.h"
#include "GameSettingsRuntime.h"
#include "GamePersistenceRuntime.h"
#include "GameRunDefaults.h"
#include "GameRunSession.h"
#include "GameRewardState.h"
#include "GameEncounterBalance.h"
#include "GameBallSelectionState.h"
#include "GamePauseState.h"
#include "GameStageOverrideState.h"
#include "RunController.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "RunProgressController.h"
#include "Camera.h"
#include "BattleViewportLayout.h"
#include "Renderer.h"
#include "BalanceLogger.h"
#include "GameMcpBridge.h"
#include "GamePresentation.h"
#include "BallCardPresentation.h"
#include "AudioFeedback.h"
#include "BossShotPlanner.h"
#include "GameDebugController.h"
#include "BattleScene.h"
#include "PlayerBall.h"
#include "EnemyBall.h"
#include "input.h"
#include "GameUi.h"
#include "imgui/imgui.h"
#include "json/json.hpp"

#pragma execution_character_set("utf-8")

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <string>

using DirectX::SimpleMath::Vector3;

namespace
{
	/// <summary>
	/// 環境変数から検証用の物理時間倍率を取得し、1倍から4倍の範囲に収める。
	/// </summary>
	double ValidationPhysicsTimeScale()
	{
		static const double scale = []
		{
			char value[32]{};
			const DWORD length = GetEnvironmentVariableA(
				"CAROMINO_VALIDATION_TIME_SCALE",
				value,
				static_cast<DWORD>(std::size(value)));
			if (length == 0 || length >= std::size(value)) return 1.0;
			char* end = nullptr;
			const double parsed = std::strtod(value, &end);
			if (end == value || !std::isfinite(parsed)) return 1.0;
			return (std::clamp)(parsed, 1.0, 4.0);
		}();
		return scale;
	}
}

namespace
{
	/// <summary>
	/// リストに含まれる撃破前の敵球を数える。
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

Game* Game::m_Instance;

/// <summary>
/// ゲームの実行状態を初期化し、戦闘シーンを生成する準備をする。
/// </summary>
Game::Game()
	: m_RuntimeState(std::make_unique<GameRuntimeState>())
	, m_Camera(Camera::GetInstance())
	, m_BattleState(std::make_unique<GameBattleState>())
	, m_RewardState(std::make_unique<GameRewardState>())
	, m_RunDefaults(std::make_unique<GameRunDefaults>())
	, m_RunSession(std::make_unique<GameRunSession>())
	, m_RunController(std::make_unique<RunController>())
	, m_StageOverrideState(std::make_unique<GameStageOverrideState>())
	, m_BossShotPlanner(std::make_unique<BossShotPlanner>())
	, m_BallSelectionState(std::make_unique<GameBallSelectionState>())
	, m_BalanceRuntime(std::make_unique<GameBalanceRuntime>())
	, m_SettingsRuntime(std::make_unique<GameSettingsRuntime>())
	, m_PersistenceRuntime(std::make_unique<GamePersistenceRuntime>())
	, m_AudioFeedback(std::make_unique<AudioFeedback>())
	, m_PendingShotTelemetry(std::make_unique<nlohmann::json>(
		nlohmann::json::object()))
	, m_EncounterBalance(std::make_unique<GameEncounterBalance>())
	, m_DebugController(std::make_unique<GameDebugController>())
	, m_PauseState(std::make_unique<GamePauseState>())
{
	InitializeBattleSceneFactory();
}

/// <summary>
/// 現在のシーンを破棄し、管理中のゲームオブジェクトをすべて解放する。
/// </summary>
Game::~Game()
{
	m_RuntimeState->ResetScene();
	DeleteAllGameObjects();
}

/// <summary>
/// 描画、入力、カメラなどを初期化し、設定や保存データを読み込んでタイトルシーンを開始する。
/// </summary>
void Game::Init()
{
	// 静的インスタンスをここで1つだけ生成
	if (m_Instance == nullptr) {
		m_Instance = new Game();
	}
	// 描画処理を初期化
	Renderer::Init();
	BallCardPresentation::LoadResources();
	m_Instance->m_SettingsRuntime->Initialize();
	const bool allowGeneratedAudioTestTones =
		std::wstring(GetCommandLineW()).find(L"--audio-test-tones") !=
		std::wstring::npos;
	m_Instance->m_AudioFeedback->Initialize(allowGeneratedAudioTestTones);

	// 入力処理を初期化
	Input::Create();

	// カメラを初期化
	m_Instance->m_Camera.Init();

	m_Instance->m_PersistenceRuntime->Profile().Load();
	m_Instance->LoadPlayerStatusFromJson();
	m_Instance->m_BalanceRuntime->autoPlayer.LoadConfig();
	m_Instance->LoadDifficultyProfileConfig();
	m_Instance->m_BalanceRuntime->scaling.LoadConfig();
	m_Instance->m_BalanceRuntime->normalPlay.Load();
	m_Instance->m_BalanceRuntime->validation.LoadConfig(
		"assets/data/balance_validation.json",
		m_Instance->m_BalanceRuntime->baselineDifficultyProfile);
	m_Instance->LoadEncounterBalanceConfig();
	m_Instance->LoadPocketRulesConfig();
	m_Instance->InitializeBattleController();

	// 最初のシーンを読み込む
	m_Instance->m_RuntimeState->Transition().Configure(m_Instance->m_SettingsRuntime->Current().sceneTransition);
	m_Instance->m_RuntimeState->InitializeScene(SceneType::Title);
	m_Instance->m_GameMcpBridge =
		std::make_unique<GameMcpBridge>();
	m_Instance->m_GameMcpBridge->Initialize(*m_Instance);
	m_Instance->m_GamePresentation =
		std::make_unique<GamePresentation>();
	m_Instance->m_GamePresentation->Initialize(*m_Instance);
	m_Instance->m_PhaseTestRunRequested =
		std::wstring(GetCommandLineW()).find(L"--phase-test-run") !=
			std::wstring::npos;
	// 保存済みの実験条件を直接開くための任意起動引数。
	if (std::wstring(GetCommandLineW()).find(L"--debug-battle") != std::wstring::npos)
	{
		m_Instance->OpenDebugMode();
		if (m_Instance->LoadDebugPreset()) m_Instance->m_DebugController->RequestStart();
	}
}

/// <summary>
/// 入力を読み取り、シーンや固定刻みの物理を更新して戦闘結果を処理する。
/// </summary>
void Game::Update(double elapsedSeconds)
{
	using TimingClock = std::chrono::steady_clock;
	UpdateTimingFrame updateTiming{};
	bool measureUpdate =
		m_Instance->m_RuntimeState->IsFrameTimingMeasurementActive();
	auto updateTimingStarted = measureUpdate
		? TimingClock::now()
		: TimingClock::time_point{};
	const auto startSection = [&]()
	{
		return measureUpdate
			? TimingClock::now()
			: TimingClock::time_point{};
	};
	const auto finishSection = [&](const TimingClock::time_point& started)
	{
		return measureUpdate
			? std::chrono::duration<double, std::milli>(
				TimingClock::now() - started).count()
			: 0.0;
	};

	m_Instance->RemoveDestroyedGameObjects();

	// 入力処理を更新
	Input::Update();
	auto& transition = m_Instance->m_RuntimeState->Transition();
	if (transition.IsPreviewing() && (Input::GetKeyTrigger(VK_ESCAPE) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
	{
		transition.CancelPreview();
		// Consume this frame so the same Esc cannot close the settings/pause UI.
		ResetFrameTiming();
		return;
	}
	if (transition.Advance(elapsedSeconds))
	{
		ResetFrameTiming();
		return;
	}
	if (m_Instance->UpdateDebugMode()) return;
	if (m_Instance->m_PauseState->ConsumeDisplayApplyRequest())
	{
		m_Instance->m_SettingsRuntime->ApplyDisplaySettings();
	}
	if (m_Instance->m_PauseState->ConsumeFullscreenToggleRequest() || Input::GetKeyTrigger(VK_F11))
	{
		m_Instance->m_SettingsRuntime->ToggleFullscreen();
	}
	if ((m_Instance->m_PauseState->ConsumePauseToggleRequest() || Input::GetKeyTrigger(VK_ESCAPE)) &&
		m_Instance->CanPause() &&
		!m_Instance->m_BalanceRuntime->autoPlayer.IsEnabled())
	{
		const bool wasPaused = m_Instance->m_PauseState->TogglePaused();
		if (wasPaused)
		{
			m_Instance->m_SettingsRuntime->SaveIfDirty();
		}
	}
	if ((m_Instance->m_PauseState->ConsumeManualSaveRequest() || Input::GetKeyTrigger(VK_F6)) &&
		!m_Instance->m_BalanceRuntime->autoPlayer.IsEnabled())
	{
		m_Instance->SaveCurrentRun();
	}
	if (m_Instance->m_PauseState->IsPaused())
	{
		ResetFrameTiming();
		return;
	}
	m_Instance->m_PersistenceRuntime->AdvanceFrame(
		m_Instance->IsRunSessionActive());
	const auto audioFeedbackStarted = startSection();
	if (m_Instance->m_AudioFeedback != nullptr)
	{
		m_Instance->m_AudioFeedback->Update(
			elapsedSeconds,
			m_Instance->GetSeVolume());
	}
	updateTiming.audioFeedbackMilliseconds =
		finishSection(audioFeedbackStarted);
	const auto presentationStarted = startSection();
	if (m_Instance->m_GamePresentation != nullptr)
	{
		m_Instance->m_GamePresentation->Update(
			*m_Instance,
			static_cast<float>(elapsedSeconds));
	}
	updateTiming.presentationMilliseconds =
		finishSection(presentationStarted);

	const auto gameMcpStarted = startSection();
	if (m_Instance->m_GameMcpBridge != nullptr)
	{
		m_Instance->m_GameMcpBridge->Update(*m_Instance);
	}
	updateTiming.gameMcpMilliseconds = finishSection(gameMcpStarted);
	if (!measureUpdate &&
		m_Instance->m_RuntimeState->IsFrameTimingMeasurementActive())
	{
		// The fire_shot command begins measurement during the MCP update.
		// Profile the remaining work without paying detailed-timer overhead
		// while measurement is disabled.
		measureUpdate = true;
		updateTimingStarted = TimingClock::now();
	}

	if (m_Instance->m_BalanceRuntime->autoPlayer.Update(*m_Instance))
	{
		ResetFrameTiming();
		return;
	}

	// ==========================
	// ClearReward中はゲーム本編を更新しない
	// ==========================
	if (m_Instance->IsClearRewardActive())
	{
		ResetFrameTiming();
		m_Instance->UpdateClearReward();
		return;
	}

	if (!m_Instance->m_BalanceRuntime->autoPlayer.IsEnabled() &&
		dynamic_cast<BattleScene*>(m_Instance->GetCurrentScene()) != nullptr &&
		m_Instance->m_RunController->Deck().GetOfferCount() > 0)
	{
		m_Instance->UpdateBallSelection(static_cast<float>(elapsedSeconds));
	}

	// シーンを更新
	const auto sceneStarted = startSection();
	if (Scene* scene = m_Instance->GetCurrentScene())
	{
		scene->Update();
	}
	updateTiming.sceneMilliseconds = finishSection(sceneStarted);
	if (m_Instance->m_RuntimeState->Transition().Active())
	{
		ResetFrameTiming();
		return;
	}

	// カメラを更新
	const auto cameraStarted = startSection();
	m_Instance->UpdateSceneCamera();

	updateTiming.cameraMilliseconds =
		finishSection(cameraStarted);

	// オブジェクトを更新
	const auto worldUpdateStarted = startSection();
	m_Instance->m_RuntimeState->World().Update();
	updateTiming.worldUpdateMilliseconds =
		finishSection(worldUpdateStarted);

	// Input/AI/MCP have run once. Only physical motion may catch up here.
	const auto fixedPhysicsStarted = startSection();
	// Validation may advance more fixed 1/60 s ticks per rendered frame. This
	// reduces wall-clock collection time without changing the physics step,
	// random seeds, input accuracy, or production behavior (default scale 1).
	m_Instance->UpdateFixedPhysics(
		elapsedSeconds * ValidationPhysicsTimeScale());
	updateTiming.fixedPhysicsMilliseconds =
		finishSection(fixedPhysicsStarted);

	const auto worldLateUpdateStarted = startSection();
	m_Instance->m_RuntimeState->World().LateUpdate();
	updateTiming.worldLateUpdateMilliseconds =
		finishSection(worldLateUpdateStarted);
	m_Instance->RemoveDestroyedGameObjects();
	const auto battleControllerStarted = startSection();
	m_Instance->m_BattleState->controller.Update(elapsedSeconds);
	updateTiming.battleControllerMilliseconds =
		finishSection(battleControllerStarted);
	if (measureUpdate)
	{
		const double measuredTotal = std::chrono::duration<double, std::milli>(
			TimingClock::now() - updateTimingStarted).count();
		const double classifiedTotal =
			updateTiming.audioFeedbackMilliseconds +
			updateTiming.presentationMilliseconds +
			updateTiming.gameMcpMilliseconds +
			updateTiming.sceneMilliseconds +
			updateTiming.cameraMilliseconds +
			updateTiming.worldUpdateMilliseconds +
			updateTiming.fixedPhysicsMilliseconds +
			updateTiming.worldLateUpdateMilliseconds +
			updateTiming.battleControllerMilliseconds;
		updateTiming.otherMilliseconds = (std::max)(
			0.0,
			measuredTotal - classifiedTotal);
		m_Instance->m_RuntimeState->RecordUpdateTiming(updateTiming);
	}

	const BattleResult battleResult =
		m_Instance->m_BattleState->controller.ConsumeResult();

	if (battleResult != BattleResult::None)
	{
		m_Instance->m_LastBattleResult = battleResult;
		m_Instance->m_BattleState->playerShield = 0;
	}

	switch (battleResult)
	{
	case BattleResult::Victory:
		m_Instance->StartClearReward();
		return;

	case BattleResult::Defeat:
		m_Instance->ProcessGameOver();
		return;

	default:
		break;
	}

}

/// <summary>
/// 戦闘画面のレイアウトに合わせてカメラの注視点と距離を調整し、カメラを更新する。
/// </summary>
void Game::UpdateSceneCamera()
{
	if (dynamic_cast<BattleScene*>(GetCurrentScene()))
	{
		const auto layout = BattleViewportLayout::Calculate(
			static_cast<float>(Application::GetWidth()), static_cast<float>(Application::GetHeight()));
		m_Camera.SetDistanceY(layout.cameraDistance);
		Vector3 target = m_Camera.GetTarget();
		target.z += layout.cameraTargetZ;
		m_Camera.SetTarget(target);
	}
	else m_Camera.SetDistanceY(150.0f);
	m_Camera.Update();
}

/// <summary>
/// ゲームワールドと各種UIを描画し、シーン遷移時はフェードを重ねる。
/// </summary>
void Game::Draw()
{
	Renderer::DrawStart();
	const bool transitioning = m_Instance->m_RuntimeState->Transition().Active();
	if (transitioning) ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.0f);
	ImGui::BeginDisabled(transitioning);

	m_Instance->m_RuntimeState->World().Draw();

	ImGui::BeginDisabled(m_Instance->IsPaused() || m_Instance->m_DebugController->IsEditorOpen());
	if (m_Instance->GetCurrentScene() != nullptr)
	{
		m_Instance->GetCurrentScene()->DrawUI();
	}

	m_Instance->m_DebugController->DrawDiagnostics(*m_Instance);
	ImGui::EndDisabled();

	// Submit world-linked presentation before interactive card windows so an
	// expanded card always remains the foremost readable combat element.
	if (m_Instance->m_GamePresentation != nullptr)
	{
		m_Instance->m_GamePresentation->Draw(*m_Instance);
	}

	ImGui::BeginDisabled(m_Instance->IsPaused() || m_Instance->m_DebugController->IsEditorOpen());
	if (dynamic_cast<BattleScene*>(m_Instance->GetCurrentScene()) != nullptr &&
		!m_Instance->IsClearRewardActive() &&
		m_Instance->m_RunController->Deck().GetOfferCount() > 0)
	{
		m_Instance->DrawBallSelectionUI();
	}

	// ==========================
	// 報酬UIを最後に重ねる
	// ==========================
	if (m_Instance->IsClearRewardActive())
	{
		m_Instance->DrawClearRewardUI();
	}

	ImGui::EndDisabled();

	m_Instance->DrawDebugMode();
	if (m_Instance->IsPaused() && !m_Instance->m_DebugController->IsEditorOpen())
	{
		m_Instance->DrawPauseUI();
	}

	if (!m_Instance->GetSaveLoadMessage().empty())
	{
		ImGui::SetNextWindowPos(GameUi::MainPosition(20.0f, ImGui::GetMainViewport()->WorkSize.y - 40.0f), ImGuiCond_Always);
		ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
		ImGui::SetNextWindowBgAlpha(0.88f);
		const ImGuiWindowFlags saveNoticeFlags =
			ImGuiWindowFlags_NoDecoration |
			ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoInputs |
			ImGuiWindowFlags_NoNav;
		ImGui::Begin("##SaveLoadNotice", nullptr, saveNoticeFlags);
		ImGui::TextUnformatted(m_Instance->GetSaveLoadMessage().c_str());
		ImGui::End();
	}

	ImGui::EndDisabled();
	const float fadeAlpha = m_Instance->m_RuntimeState->Transition().Alpha();
	if (transitioning) ImGui::PopStyleVar();
	if (fadeAlpha > 0)
	{
		for (auto* viewport : ImGui::GetPlatformIO().Viewports)
			ImGui::GetForegroundDrawList(viewport)->AddRectFilled(viewport->Pos,
				ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y),
				IM_COL32(0, 0, 0, static_cast<int>(255 * fadeAlpha)));
	}
	// Application owns ImGui rendering and presentation, including detached windows.
}

/// <summary>
/// 設定の保存とプレイ記録の終了処理を行い、シーンやゲームのリソースを解放する。
/// </summary>
void Game::Uninit()
{
	if (m_Instance != nullptr)
	{
		m_Instance->m_SettingsRuntime->SaveIfDirty();
		m_Instance->m_AudioFeedback.reset();
		m_Instance->m_GamePresentation.reset();
		if (m_Instance->m_GameMcpBridge != nullptr)
		{
			m_Instance->m_GameMcpBridge->Shutdown(*m_Instance);
		}

		const std::vector<PlayerBall*> players =
			GameRuntime::World().GetComponents<PlayerBall>();
		const std::vector<EnemyBall*> enemies =
			GameRuntime::World().GetComponents<EnemyBall>();
		const int playerHp =
			players.empty() || players[0] == nullptr
			? m_Instance->m_RunController->Status().currentHp
			: players[0]->GetHP();

		BalanceLogger& logger =
			BalanceLogger::GetInstance();
		logger.EndShot(
			playerHp,
			CountAliveEnemies(enemies),
			m_Instance->m_BattleState->controller.GetDefeatedEnemyCount());
		logger.EndStage(
			"application_exit",
			playerHp,
			m_Instance->m_RunController->Status().maxHp,
			m_Instance->m_BattleState->controller.GetDefeatedEnemyCount());
		logger.EndRun(
			"application_exit",
			playerHp,
			m_Instance->m_RunController->Status().maxHp,
			m_Instance->m_RunController->Progress().GetClearedBattleCount());
	}

	// Release scene-owned observers and GPU resources while the world is alive.
	m_Instance->m_RuntimeState->ResetScene();

	// カメラの終了処理
	m_Instance->m_Camera.Uninit();

	// オブジェクトの終了処理は所有者であるGameWorldへ委譲
	m_Instance->m_RuntimeState->World().Clear();

	// 入力処理を終了
	Input::Release();

	// 描画処理を終了
	BallCardPresentation::ResetResources();
	IllustratedUi::ResetResources();
	Renderer::Uninit();

	// インスタンスを削除
	delete m_Instance;
}

/// <summary>
/// 共有ゲーム管理オブジェクトへのポインターを返す。
/// </summary>
Game* Game::GetInstance()
{
	return m_Instance;
}
