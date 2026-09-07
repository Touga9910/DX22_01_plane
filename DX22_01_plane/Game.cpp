#include "Game.h"
#include "BallShotPrediction.h"
#include "GameUi.h"
#include "BallMechanics.h"

#pragma execution_character_set("utf-8")
#include "Renderer.h"
#include "BalanceLogger.h"
#include "GameMcpBridge.h"
#include "GamePresentation.h"
#include "GameSaveManager.h"
#include "BallPhysicsComponent.h"
#include "BallPhysicsWorld.h"
#include "input.h"

#include "PlayerBall.h"  // DrawImGui呼び出しに必要
#include "PlayerBallText.h"
#include "PlayerBallUI.h"
#include "EnemyBall.h"
#include "BreakBall.h"   // DrawImGui呼び出しに必要
#include "EnemyAttackComponent.h"
#include "BallComponent.h"
#include "PlayerBallDataLoader.h"
#include "StageDataLoader.h"
#include "EnemyData.h"
#include "TableConfig.h"
#include "UiText.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <stdexcept>

using DirectX::SimpleMath::Vector3;

Game* Game::m_Instance;//ゲームインスタンス

namespace
{
	const char* const kClearRewardNames[] =
	{
		UiText::RewardNewBall,
		UiText::RewardUpgrade,
		UiText::RewardExtraMoney,
	};
	constexpr int kClearRewardCount =
		static_cast<int>(sizeof(kClearRewardNames) / sizeof(kClearRewardNames[0]));
	constexpr int kExtraRewardMoney = 10;
	constexpr int kAutoShopRemoveCost = 15;
	constexpr int kBankShotDamageMultiplier = 2;
	constexpr int kEmergencyRepairContactThreshold = 3;
	constexpr int kEmergencyRepairHealAmount = 1;

	const char* GetPlayerBallDisplayName(const std::string& definitionId)
	{
		return PlayerBallText::GetName(definitionId);
	}

	const char* GetPlayerBallAbilityName(const PlayerBallData& ball)
	{
		return PlayerBallText::GetTrait(ball);
	}

	int CountDefeatedEnemies(const std::vector<EnemyBall*>& enemies)
	{
		return static_cast<int>(std::count_if(
			enemies.begin(),
			enemies.end(),
			[](const EnemyBall* enemy)
			{
				return enemy != nullptr && enemy->IsDefeated();
			}));
	}

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

	bool CanEnemyAttackThisTurn(const EnemyBall* enemy)
	{
		if (enemy == nullptr ||
			enemy->IsDefeated() ||
			enemy->IsPocketed())
		{
			return false;
		}

		GameObject* owner = enemy->GetGameObject();
		return owner != nullptr &&
			owner->GetComponent<EnemyAttackComponent>() != nullptr;
	}

	const char* GetGameStateDebugName(GameState state)
	{
		switch (state)
		{
		case GameState::AimingDirection: return "AimingDirection";
		case GameState::AimingPower:     return "AimingPower";
		case GameState::ConfirmShot:     return "ConfirmShot";
		case GameState::BallsMoving:     return "BallsMoving";
		case GameState::EnemyAttack:     return "EnemyAttack";
		case GameState::TurnEnd:         return "TurnEnd";
		case GameState::ClearReward:     return "ClearReward";
		case GameState::GameOver:        return "GameOver";
		default:                         return "Unknown";
		}
	}

	const char* GetIncomingDamagePhaseLabel(GameState state)
	{
		switch (state)
		{
		case GameState::AimingDirection:
		case GameState::AimingPower:
		case GameState::ConfirmShot:
		case GameState::BallsMoving:
			return "このターンの敵攻撃前";
		case GameState::EnemyAttack:
			return "敵攻撃処理中";
		case GameState::TurnEnd:
			return "このターンの敵攻撃は処理済み（次ターン参考）";
		case GameState::ClearReward:
			return "敵全滅（被ダメージなし）";
		case GameState::GameOver:
			return "ゲームオーバー（参考値）";
		default:
			return "戦闘外（参考値）";
		}
	}

	const char* GetIncomingDamageValueLabel(GameState state)
	{
		switch (state)
		{
		case GameState::AimingDirection:
		case GameState::AimingPower:
		case GameState::ConfirmShot:
		case GameState::BallsMoving:
		case GameState::EnemyAttack:
			return "このターンの予測被ダメージ";
		case GameState::TurnEnd:
			return "次ターンの参考被ダメージ";
		default:
			return "参考被ダメージ";
		}
	}

	const char* GetSceneDebugName(Scene* scene)
	{
		if (dynamic_cast<TitleScene*>(scene)) return "TITLE";
		if (dynamic_cast<StageSelectScene*>(scene)) return "SELECT";
		if (dynamic_cast<BattleScene*>(scene)) return "BATTLE";
		if (dynamic_cast<RestSiteScene*>(scene)) return "REST_SITE";
		if (dynamic_cast<ShopScene*>(scene)) return "SHOP";
		if (dynamic_cast<ResultScene*>(scene)) return "RESULT";

		return "Unknown";
	}

	void WriteVector3(std::ofstream& file, const char* label, const DirectX::SimpleMath::Vector3& value)
	{
		file << label << " = ("
			<< value.x << ", "
			<< value.y << ", "
			<< value.z << ")\n";
	}

	void WriteBallDebugStatus(std::ofstream& file, const char* typeName, int index, BallComponent* ball)
	{
		if (ball == nullptr)
		{
			return;
		}

		file << "[" << typeName << " " << index << "]\n";
		WriteVector3(file, "Position", ball->GetPosition());
		WriteVector3(file, "Velocity", ball->GetVelocity());
		file << "HP = " << ball->GetHP() << " / " << ball->GetMaxHP() << "\n";
		file << "Radius = " << ball->GetRadius() << "\n";
		file << "IsStopped = " << (ball->IsStopped() ? "true" : "false") << "\n";
		file << "IsDefeated = " << (ball->IsDefeated() ? "true" : "false") << "\n";
		file << "\n";
	}
}

// コンストラクタ
Game::Game()
{
	m_Scene = nullptr;
}

// デストラクタ
Game::~Game()
{
	delete m_Scene;
	DeleteAllGameObjects();
}

void Game::InvalidateDebugCombatForecast(const char* reason)
{
	m_DebugCombatForecastDirty = true;
	if (reason != nullptr && reason[0] != '\0')
	{
		m_DebugCombatForecastPendingReason = reason;
	}
}

void Game::RefreshDebugCombatForecast()
{
	if (!m_DebugCombatForecastDirty)
	{
		return;
	}

	DebugCombatForecastSnapshot next{};
	next.updateRevision = m_DebugCombatForecast.updateRevision + 1;
	next.updateReason = m_DebugCombatForecastPendingReason;

	const std::vector<PlayerBall*> players = GetComponents<PlayerBall>();
	const std::vector<EnemyBall*> enemies = GetComponents<EnemyBall>();
	const PlayerBall* player = players.empty() ? nullptr : players.front();
	if (player != nullptr)
	{
		next.hasPlayer = true;
		next.playerCurrentHp = player->GetHP();
		next.playerMaxHp = player->GetMaxHP();
		next.playerDefense = player->GetDefense();
		next.hpAfterAttack = (std::max)(0, player->GetHP());
	}

	next.enemies.reserve(enemies.size());
	for (const EnemyBall* enemy : enemies)
	{
		if (enemy == nullptr)
		{
			continue;
		}

		DebugEnemyCombatSnapshot enemySnapshot{};
		enemySnapshot.id = enemy->GetEnemyId();
		enemySnapshot.currentHp = enemy->GetHP();
		enemySnapshot.maxHp = enemy->GetMaxHP();
		enemySnapshot.attack = enemy->GetAttack();
		const bool canAttack = CanEnemyAttackThisTurn(enemy);
		enemySnapshot.defeated = enemy->IsDefeated();
		enemySnapshot.pocketed = enemy->IsPocketed();
		enemySnapshot.canAttack = canAttack;
		enemySnapshot.state = enemy->IsDefeated()
			? "撃破"
			: enemy->IsPocketed()
				? "ポケット中"
				: canAttack
					? "攻撃予定"
					: "攻撃不可";

		if (canAttack && player != nullptr && !player->IsDefeated())
		{
			next.attackerCount++;
			enemySnapshot.damageBeforeMinimum =
				enemy->GetAttack() - player->GetDefense();
			enemySnapshot.expectedDamage =
				player->CalculateDamageTaken(enemy->GetAttack());
			enemySnapshot.minimumDamageApplied =
				enemySnapshot.damageBeforeMinimum < 1;
			next.theoreticalDamage += enemySnapshot.expectedDamage;
			const int appliedDamage = (std::min)(
				next.hpAfterAttack,
				enemySnapshot.expectedDamage);
			next.expectedDamage += appliedDamage;
			next.hpAfterAttack -= appliedDamage;
		}

		next.enemies.push_back(std::move(enemySnapshot));
	}

	next.overkillDamage = (std::max)(
		0,
		next.theoreticalDamage - next.playerCurrentHp);
	next.lethal = next.hasPlayer &&
		next.playerCurrentHp > 0 &&
		next.hpAfterAttack <= 0;
	m_DebugCombatForecast = std::move(next);
	m_DebugCombatForecastDirty = false;
}

void Game::RecordDebugPlayerDamage(
	const std::string& source,
	const std::string& sourceId,
	int damage,
	int hpBefore,
	int hpAfter)
{
	if (damage <= 0)
	{
		return;
	}

	DebugPlayerDamageRecord record{};
	record.sequence = ++m_DebugDamageSequence;
	record.source = source;
	record.sourceId = sourceId;
	record.damage = damage;
	record.hpBefore = hpBefore;
	record.hpAfter = hpAfter;
	m_DebugPlayerDamageHistory.push_front(std::move(record));
	constexpr std::size_t kMaximumHistory = 10;
	while (m_DebugPlayerDamageHistory.size() > kMaximumHistory)
	{
		m_DebugPlayerDamageHistory.pop_back();
	}
}

bool Game::CanPause() const
{
	return m_RunActive &&
		(dynamic_cast<BattleScene*>(m_Scene) != nullptr ||
		 dynamic_cast<StageSelectScene*>(m_Scene) != nullptr ||
		 dynamic_cast<RestSiteScene*>(m_Scene) != nullptr ||
		 dynamic_cast<ShopScene*>(m_Scene) != nullptr);
}

bool Game::SaveAndReturnToTitle()
{
	if (m_DebugMode) { m_DebugRequest = 2; return true; }
	if (m_GameState == GameState::ClearReward && !m_IsClearRewardChosen)
	{
		SetSaveLoadMessage(
			"報酬を選択して次のルートへ進んでから保存してください。");
		return false;
	}

	CaptureCurrentPlayerStatus();
	SceneType resumeScene = SceneType::Max;
	bool sceneAlreadyActive = true;
	if (dynamic_cast<BattleScene*>(m_Scene) != nullptr)
	{
		resumeScene = m_GameState == GameState::ClearReward
			? SceneType::Select
			: SceneType::Battle;
		sceneAlreadyActive = false;
	}
	else if (dynamic_cast<StageSelectScene*>(m_Scene) != nullptr)
	{
		resumeScene = SceneType::Select;
	}
	else if (dynamic_cast<RestSiteScene*>(m_Scene) != nullptr)
	{
		resumeScene = SceneType::RestSite;
	}
	else if (dynamic_cast<ShopScene*>(m_Scene) != nullptr)
	{
		resumeScene = SceneType::Shop;
	}

	if (resumeScene == SceneType::Max ||
		!SaveRunCheckpoint(resumeScene, sceneAlreadyActive, true))
	{
		return false;
	}

	m_IsPaused = false;
	m_PauseConfirmTitle = false;
	m_RunActive = false;
	ChangeScene(SceneType::Title);
	return true;
}

// 初期化
void Game::Init()
{
	// 静的インスタンスをここで1つだけ生成
	if (m_Instance == nullptr) {
		m_Instance = new Game();
	}
	// 描画処理を初期化
	Renderer::Init();
	m_Instance->m_SettingsManager.Load();
	m_Instance->m_SettingsManager.ApplyDisplaySettings();

	// 入力処理を初期化
	Input::Create();

	// カメラを初期化
	m_Instance->m_Camera.Init();

	m_Instance->m_ProgressionProfile.Load();
	m_Instance->LoadPlayerStatusFromJson();
	m_Instance->LoadBalanceAutoPlayConfig();
	m_Instance->LoadDifficultyProfileConfig();
	m_Instance->LoadDynamicBalanceConfig();
	m_Instance->LoadBalanceValidationConfig();
	m_Instance->LoadEncounterBalanceConfig();
	m_Instance->LoadPocketRulesConfig();

	// 最初のシーンを読み込む
	m_Instance->m_Scene = new TitleScene;
	m_Instance->m_GameMcpBridge =
		std::make_unique<GameMcpBridge>();
	m_Instance->m_GameMcpBridge->Initialize(*m_Instance);
	m_Instance->m_GamePresentation =
		std::make_unique<GamePresentation>();
	m_Instance->m_GamePresentation->Initialize(*m_Instance);
	// 保存済みの実験条件を直接開くための任意起動引数。
	if (std::wstring(GetCommandLineW()).find(L"--debug-battle") != std::wstring::npos)
	{
		m_Instance->OpenDebugMode();
		if (m_Instance->LoadDebugPreset()) m_Instance->m_DebugRequest = 1;
	}
}

// 更新
void Game::Update(double elapsedSeconds)
{
	m_Instance->RemoveDestroyedGameObjects();

	// 入力処理を更新
	Input::Update();
	if (m_Instance->UpdateDebugMode()) return;
	if (m_Instance->m_PendingDisplayApply)
	{
		m_Instance->m_SettingsManager.ApplyDisplaySettings();
		m_Instance->m_PendingDisplayApply = false;
	}
	if (std::exchange(m_Instance->m_MouseFullscreenToggle, false) || Input::GetKeyTrigger(VK_F11))
	{
		GameSettings& settings = m_Instance->m_SettingsManager.Edit();
		settings.fullscreen = !settings.fullscreen;
		m_Instance->m_SettingsManager.MarkDirty();
		m_Instance->m_SettingsManager.ApplyDisplaySettings();
		m_Instance->m_SettingsManager.SaveIfDirty();
	}
	if ((std::exchange(m_Instance->m_MousePauseToggle, false) || Input::GetKeyTrigger(VK_ESCAPE)) &&
		m_Instance->CanPause() &&
		!m_Instance->m_BalanceAutoPlayEnabled)
	{
		const bool wasPaused = m_Instance->m_IsPaused;
		m_Instance->m_IsPaused = !m_Instance->m_IsPaused;
		m_Instance->m_PauseConfirmTitle = false;
		if (wasPaused)
		{
			m_Instance->m_SettingsManager.SaveIfDirty();
		}
	}
	if ((std::exchange(m_Instance->m_MouseSaveRequested, false) || Input::GetKeyTrigger(VK_F6)) &&
		!m_Instance->m_BalanceAutoPlayEnabled)
	{
		m_Instance->SaveCurrentRun();
	}
	if (m_Instance->m_IsPaused)
	{
		ResetFrameTiming();
		return;
	}
	if (m_Instance->m_RunActive)
	{
		m_Instance->m_RunStatistics.AdvanceFrame();
	}
	if (m_Instance->m_SaveLoadMessageFrames > 0)
	{
		--m_Instance->m_SaveLoadMessageFrames;
		if (m_Instance->m_SaveLoadMessageFrames == 0)
		{
			m_Instance->m_SaveLoadMessage.clear();
		}
	}
	if (m_Instance->m_GamePresentation != nullptr)
	{
		m_Instance->m_GamePresentation->Update(*m_Instance);
	}

	if (m_Instance->m_GameMcpBridge != nullptr)
	{
		m_Instance->m_GameMcpBridge->Update(*m_Instance);
	}

	if (m_Instance->UpdateBalanceAutoPlay())
	{
		ResetFrameTiming();
		return;
	}

	// ==========================
	// ClearReward中はゲーム本編を更新しない
	// ==========================
	if (m_Instance->m_GameState == GameState::ClearReward)
	{
		ResetFrameTiming();
		m_Instance->UpdateClearReward();
		return;
	}

	if (!m_Instance->m_BalanceAutoPlayEnabled &&
		dynamic_cast<BattleScene*>(m_Instance->m_Scene) != nullptr &&
		m_Instance->m_PlayerDeck.GetOfferCount() > 0)
	{
		m_Instance->UpdateBallSelection();
	}

	// シーンを更新
	m_Instance->m_Scene->Update();

	// カメラを更新
	m_Instance->m_Camera.Update();

	// オブジェクトを更新
	for (auto& gameObject : m_Instance->m_GameObjects)
	{
		gameObject->Update();
	}

	// Input/AI/MCP have run once. Only physical motion may catch up here.
	m_Instance->UpdateFixedPhysics(elapsedSeconds);

	for (auto& gameObject : m_Instance->m_GameObjects)
	{
		gameObject->LateUpdate();
	}
	m_Instance->RemoveDestroyedGameObjects();
	if (m_Instance->TryRecoverClearedBattle("state_invariant"))
	{
		return;
	}

	// ゲーム状態を更新（全ボールの停止検知など）
	switch (m_Instance->m_GameState)
	{
	case GameState::BallsMoving:
		// The 11-tick settling counter is advanced only by fixed physics.
		if (m_Instance->m_AllBallsStoppedFrameCount >= 11)
		{
			m_Instance->m_AllBallsStoppedFrameCount = 0;
			for (GameObject* ball :
				m_Instance->GetGameObjectsWith<BallPhysicsComponent>())
			{
				if (ball == nullptr)
				{
					continue;
				}
				if (BallPhysicsComponent* physics =
					ball->GetComponent<BallPhysicsComponent>())
				{
					physics->Velocity() =
						DirectX::SimpleMath::Vector3::Zero;
					physics->Acceleration() =
						DirectX::SimpleMath::Vector3::Zero;
				}
			}
			m_Instance->RestorePocketedPlayer();

			const std::vector<PlayerBall*> players =
				m_Instance->GetComponents<PlayerBall>();
			const std::vector<EnemyBall*> enemies =
				m_Instance->GetComponents<EnemyBall>();
			if (!players.empty())
			{
				m_Instance->ApplyEndOfShotRelicEffects(players[0]);
			}
			const int playerHp =
				players.empty() || players[0] == nullptr
				? m_Instance->m_PlayerRunStatus.currentHp
				: players[0]->GetHP();

			BalanceLogger::GetInstance().EndShot(
				playerHp,
				CountAliveEnemies(enemies),
				CountDefeatedEnemies(enemies));
			m_Instance->FinishDynamicBalanceShot();
            for (auto* enemy : enemies) enemy->EndBossShot();
            if (!m_Instance->AreAllEnemiesDefeated())
                for (auto* neutral : m_Instance->GetComponents<BreakBall>()) neutral->Reposition();

			if (m_Instance->AreAllEnemiesDefeated())
			{
				m_Instance->StartClearReward();

				// 攻撃フェーズへ進まないのでリターン
				return;
			}

			m_Instance->m_GameState = GameState::EnemyAttack;
		}
		break;

	case GameState::EnemyAttack:
		m_Instance->ProcessEnemyAttack();
		break;

	case GameState::TurnEnd:
		m_Instance->PrepareNextPlayerBall();
		break;

	case GameState::GameOver:
		m_Instance->ProcessGameOver();
		break;

	default:
		break;
	}

}

void Game::ResetFrameTiming()
{
	if (m_Instance != nullptr)
	{
		m_Instance->m_PhysicsClock.Reset();
		m_Instance->m_ResetPhysicsElapsed = true;
		m_Instance->m_PhysicsStepsLastFrame = 0;
	}
}

void Game::UpdateFixedPhysics(double elapsedSeconds)
{
	m_PhysicsStepsLastFrame = 0;
	if (m_ResetPhysicsElapsed)
	{
		// Start a new time interval without simulating time before this scene/shot.
		elapsedSeconds = FixedStepClock::StepSeconds;
		m_ResetPhysicsElapsed = false;
	}
	const int steps = m_PhysicsClock.Advance(elapsedSeconds);
	for (int step = 0; step < steps; ++step)
	{
		const GameState previousState = m_GameState;
		for (auto& gameObject : m_GameObjects)
		{
			gameObject->FixedUpdate();
		}
		const auto physicsResult = BallPhysicsWorld::Step(*this);
		m_PhysicsSubstepsLastTick = physicsResult.substeps;
		if (physicsResult.limitReached) ++m_PhysicsSubstepLimitCount;
		++m_PhysicsTickCount;
		++m_PhysicsStepsLastFrame;
		if (m_GameState == GameState::BallsMoving)
		{
			m_AllBallsStoppedFrameCount = AreAllBallsStopped()
				? m_AllBallsStoppedFrameCount + 1 : 0;
		}
		if (m_GameState != previousState || m_AllBallsStoppedFrameCount >= 11)
		{
			if (previousState == GameState::BallsMoving) BallShotPrediction::WriteVerificationActual(*this);
			// Finish the shot / handle death once, outside the catch-up loop.
			m_PhysicsClock.Reset();
			break;
		}
	}
}

// 描画
void Game::Draw()
{
	Renderer::DrawStart();

	for (auto& gameObject : m_Instance->m_GameObjects)
	{
		gameObject->Draw();
	}

	ImGui::BeginDisabled(m_Instance->m_IsPaused || m_Instance->m_DebugEditorOpen);
	if (m_Instance->m_Scene != nullptr)
	{
		m_Instance->m_Scene->DrawUI();
	}

	if (GameUi::showDebugger)
	{
	GameUi::PrepareWindow("debugger", ImVec2(20, 70), ImVec2(570, 620));
	ImGui::Begin("Ball Debugger", &GameUi::showDebugger);
	const std::vector<PlayerBall*> players =
		m_Instance->GetComponents<PlayerBall>();
	const std::vector<EnemyBall*> enemies =
		m_Instance->GetComponents<EnemyBall>();
	if (m_Instance->m_DebugCombatForecastDirty)
	{
		m_Instance->RefreshDebugCombatForecast();
	}
	const DebugCombatForecastSnapshot& forecast =
		m_Instance->m_DebugCombatForecast;
	auto drawHpBar = [](const char* id, int currentHp, int maxHp)
	{
		const float hpRatio = maxHp <= 0
			? 0.0f
			: std::clamp(
				static_cast<float>(currentHp) / static_cast<float>(maxHp),
				0.0f,
				1.0f);
		const ImVec4 hpColor = hpRatio < 0.25f
			? ImVec4(0.90f, 0.18f, 0.18f, 1.0f)
			: hpRatio < 0.50f
				? ImVec4(0.95f, 0.70f, 0.12f, 1.0f)
				: ImVec4(0.20f, 0.75f, 0.30f, 1.0f);
		const std::string overlay =
			std::to_string(currentHp) + " / " + std::to_string(maxHp);
		ImGui::PushID(id);
		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, hpColor);
		ImGui::ProgressBar(hpRatio, ImVec2(-1.0f, 0.0f), overlay.c_str());
		ImGui::PopStyleColor();
		ImGui::PopID();
	};

	ImGui::TextUnformatted("戦闘状況（状態変化時に更新）");
	ImGui::Separator();
	ImGui::TextDisabled(
		"フェーズ: %s",
		GetIncomingDamagePhaseLabel(m_Instance->m_GameState));
	if (forecast.hasPlayer)
	{
		ImGui::Text(
			"プレイヤーHP: %d / %d  防御: %d",
			forecast.playerCurrentHp,
			forecast.playerMaxHp,
			forecast.playerDefense);
		drawHpBar(
			"player_hp",
			forecast.playerCurrentHp,
			forecast.playerMaxHp);

		const ImVec4 damageColor = forecast.lethal
			? ImVec4(1.0f, 0.25f, 0.25f, 1.0f)
			: forecast.expectedDamage > 0
				? ImVec4(1.0f, 0.75f, 0.20f, 1.0f)
				: ImVec4(0.45f, 1.0f, 0.45f, 1.0f);
		ImGui::TextColored(
			damageColor,
			"%s: %d  攻撃後HP: %d%s",
			GetIncomingDamageValueLabel(m_Instance->m_GameState),
			forecast.expectedDamage,
			forecast.hpAfterAttack,
			forecast.lethal ? "  [致死]" : "");
		ImGui::Text(
			"理論: %d  実HP減少: %d  超過: %d",
			forecast.theoreticalDamage,
			forecast.expectedDamage,
			forecast.overkillDamage);
		ImGui::TextDisabled(
			"攻撃予定: %d体（撃破・ポケット中の敵は除外）",
			forecast.attackerCount);
	}
	else
	{
		ImGui::TextDisabled("プレイヤーHP: 対象なし");
		ImGui::TextDisabled("このターンの予測被ダメージ: 算出不可");
	}
	ImGui::TextDisabled(
		"更新 #%llu: %s",
		static_cast<unsigned long long>(forecast.updateRevision),
		forecast.updateReason.c_str());
	if (m_Instance->m_DebugLastEnemyAttackComparisonValid)
	{
		const int difference =
			m_Instance->m_DebugLastEnemyAttackActualDamage -
			m_Instance->m_DebugLastEnemyAttackPredictedDamage;
		const ImVec4 comparisonColor = difference == 0
			? ImVec4(0.45f, 1.0f, 0.45f, 1.0f)
			: ImVec4(1.0f, 0.30f, 0.30f, 1.0f);
		ImGui::TextColored(
			comparisonColor,
			"直近敵攻撃  予測: %d  実測: %d  差分: %+d",
			m_Instance->m_DebugLastEnemyAttackPredictedDamage,
			m_Instance->m_DebugLastEnemyAttackActualDamage,
			difference);
	}

	ImGui::TextUnformatted("エネミーHP:");
	ImGui::Checkbox(
		"攻撃予定のみ",
		&m_Instance->m_DebugOnlyAttackers);
	ImGui::SameLine();
	ImGui::Checkbox(
		"撃破済み",
		&m_Instance->m_DebugShowDefeatedEnemies);
	ImGui::SameLine();
	ImGui::Checkbox(
		"ポケット中",
		&m_Instance->m_DebugShowPocketedEnemies);
	const char* sortLabels[] = { "生成順", "HP昇順", "危険度順" };
	if (ImGui::BeginCombo(
		"敵の並び順",
		sortLabels[m_Instance->m_DebugEnemySortMode]))
	{
		for (int mode = 0; mode < 3; ++mode)
		{
			const bool selected = mode == m_Instance->m_DebugEnemySortMode;
			if (ImGui::Selectable(sortLabels[mode], selected))
			{
				m_Instance->m_DebugEnemySortMode = mode;
			}
			if (selected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	std::vector<std::size_t> visibleEnemyIndices;
	visibleEnemyIndices.reserve(forecast.enemies.size());
	for (std::size_t i = 0; i < forecast.enemies.size(); ++i)
	{
		const DebugEnemyCombatSnapshot& enemy = forecast.enemies[i];
		if ((!m_Instance->m_DebugShowDefeatedEnemies && enemy.defeated) ||
			(!m_Instance->m_DebugShowPocketedEnemies && enemy.pocketed) ||
			(m_Instance->m_DebugOnlyAttackers && !enemy.canAttack))
		{
			continue;
		}
		visibleEnemyIndices.push_back(i);
	}
	if (m_Instance->m_DebugEnemySortMode == 1)
	{
		std::stable_sort(
			visibleEnemyIndices.begin(),
			visibleEnemyIndices.end(),
			[&forecast](std::size_t left, std::size_t right)
			{
				const DebugEnemyCombatSnapshot& a = forecast.enemies[left];
				const DebugEnemyCombatSnapshot& b = forecast.enemies[right];
				return static_cast<long long>(a.currentHp) * b.maxHp <
					static_cast<long long>(b.currentHp) * a.maxHp;
			});
	}
	else if (m_Instance->m_DebugEnemySortMode == 2)
	{
		std::stable_sort(
			visibleEnemyIndices.begin(),
			visibleEnemyIndices.end(),
			[&forecast](std::size_t left, std::size_t right)
			{
				return forecast.enemies[left].expectedDamage >
					forecast.enemies[right].expectedDamage;
			});
	}

	if (visibleEnemyIndices.empty())
	{
		ImGui::TextDisabled("  対象なし");
	}
	for (std::size_t index : visibleEnemyIndices)
	{
		const DebugEnemyCombatSnapshot& enemy = forecast.enemies[index];
		ImGui::Text(
			"  #%zu %s: %d / %d  攻撃: %d  予測: %d  [%s]",
			index + 1,
			enemy.id.c_str(),
			enemy.currentHp,
			enemy.maxHp,
			enemy.attack,
			enemy.expectedDamage,
			enemy.state.c_str());
		drawHpBar(
			("enemy_hp_" + std::to_string(index)).c_str(),
			enemy.currentHp,
			enemy.maxHp);
		if (enemy.canAttack)
		{
			ImGui::TextDisabled(
				"    攻撃%d - 防御%d = %d%s",
				enemy.attack,
				forecast.playerDefense,
				enemy.expectedDamage,
				enemy.minimumDamageApplied ? "（最低保証）" : "");
		}
	}

	if (ImGui::CollapsingHeader("直近の被ダメージ履歴"))
	{
		if (m_Instance->m_DebugPlayerDamageHistory.empty())
		{
			ImGui::TextDisabled("記録なし");
		}
		for (const DebugPlayerDamageRecord& record :
			m_Instance->m_DebugPlayerDamageHistory)
		{
			if (record.hpBefore >= 0 && record.hpAfter >= 0)
			{
				ImGui::Text(
					"#%llu %s/%s: %d -> %d  (-%d)",
					static_cast<unsigned long long>(record.sequence),
					record.source.c_str(),
					record.sourceId.empty() ? "-" : record.sourceId.c_str(),
					record.hpBefore,
					record.hpAfter,
					record.damage);
			}
			else
			{
				ImGui::Text(
					"#%llu %s/%s: -%d",
					static_cast<unsigned long long>(record.sequence),
					record.source.c_str(),
					record.sourceId.empty() ? "-" : record.sourceId.c_str(),
					record.damage);
			}
		}
	}
	ImGui::Separator();

	ImGui::Text("GameState = %d", static_cast<int>(m_Instance->m_GameState));

	ImGui::Text("AreAllBallsStopped = %s",
		m_Instance->AreAllBallsStopped() ? "true" : "false");

	ImGui::Text("AreAllEnemiesDefeated = %s",
		m_Instance->AreAllEnemiesDefeated() ? "true" : "false");

	ImGui::Text("Player Run HP = %d / %d",
		m_Instance->m_PlayerRunStatus.currentHp,
		m_Instance->m_PlayerRunStatus.maxHp);
	ImGui::Text("Draw Pile Count = %d", m_Instance->GetPlayerDeckCount());
	ImGui::Text("Discard Pile Count = %d", m_Instance->GetPlayerDiscardCount());
	ImGui::Text("Offer Count = %d", m_Instance->m_PlayerDeck.GetOfferCount());
	ImGui::Text("Total Deck Count = %d", m_Instance->m_PlayerDeck.GetRewardTargetCount());
	ImGui::Text("Current Ball Used = %s",
		m_Instance->m_PlayerDeck.IsCurrentUsed() ? "true" : "false");

	const PlayerBallData* currentDebugBall =
		m_Instance->m_PlayerDeck.GetCurrent();
	if (currentDebugBall != nullptr)
	{
		ImGui::Text(
			"Current Ball ID = %s",
			currentDebugBall->definitionId.c_str()
		);

		ImGui::Text(
			"Current Ball Attack = %d",
			currentDebugBall->status.attack
		);
	}
	else
	{
		ImGui::Text("Current Ball ID = none");
	}

	ImGui::BeginDisabled(m_Instance->m_DebugMode);
	if (ImGui::Button(
		m_Instance->m_BalanceAutoPlayEnabled
			? "Stop Balance Auto Play"
			: "Start Balance Auto Play"))
	{
		m_Instance->m_BalanceAutoPlayEnabled =
			!m_Instance->m_BalanceAutoPlayEnabled;
		m_Instance->m_AutoDecisionFrame = 0;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::Text(
		"F8 / Runs: %d%s",
		m_Instance->m_AutoRunCount,
		m_Instance->m_AutoMaxRuns > 0
			? " (limited)"
			: " (unlimited)");
	if (ImGui::Button("Save Debug Snapshot"))
	{
		m_Instance->SaveDebugSnapshot();
	}
	ImGui::SameLine();
	ImGui::TextUnformatted("debug_state_snapshot.txt");

	for (int i = 0; i < players.size(); i++)
	{
		players[i]->DrawImGui();
	}

	for (int i = 0; i < enemies.size(); i++)
	{
		std::string label = "EnemyBall " + std::to_string(i);
		enemies[i]->DrawImGui(label);
	}

	ImGui::Text(
		"Player Money = %d",
		m_Instance->m_PlayerRunStatus.money
	);

	ImGui::End();

	}

	if (dynamic_cast<BattleScene*>(m_Instance->m_Scene) != nullptr &&
		m_Instance->m_PlayerDeck.GetOfferCount() > 0)
	{
		m_Instance->DrawBallSelectionUI();
	}

	// ==========================
	// 報酬UIを最後に重ねる
	// ==========================
	if (m_Instance->m_GameState == GameState::ClearReward)
	{
		m_Instance->DrawClearRewardUI();
	}

	ImGui::EndDisabled();

	if (m_Instance->m_GamePresentation != nullptr)
	{
		m_Instance->m_GamePresentation->Draw(*m_Instance);
	}

	m_Instance->DrawDebugMode();
	if (m_Instance->m_IsPaused && !m_Instance->m_DebugEditorOpen)
	{
		m_Instance->DrawPauseUI();
	}

	if (!m_Instance->m_SaveLoadMessage.empty())
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
		ImGui::TextUnformatted(m_Instance->m_SaveLoadMessage.c_str());
		ImGui::End();
	}

	// Application owns ImGui rendering and presentation, including detached windows.
}

void Game::DrawPauseUI()
{
	GameSettings& settings = m_SettingsManager.Edit();
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.61f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	const ImGuiWindowFlags dimmerFlags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoInputs |
		ImGuiWindowFlags_NoNav |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoBringToFrontOnFocus;
	ImGui::Begin("##PauseDimmer", nullptr, dimmerFlags);
	ImGui::End();
	ImGui::PopStyleVar(2);

	GameUi::PrepareWindow("pause", ImVec2(335, 35), ImVec2(610, 650));
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
	ImGui::Begin("ポーズ", nullptr, flags);

	if (ImGui::Button("ポーズ解除", ImVec2(-1.0f, 42.0f)))
	{
		m_SettingsManager.SaveIfDirty();
		m_IsPaused = false;
		m_PauseConfirmTitle = false;
	}
	ImGui::TextDisabled("Escでもゲームへ戻れます");
	ImGui::SeparatorText("音量");
	bool settingsChanged = false;
	int bgmPercent = static_cast<int>(std::lround(settings.bgmVolume * 100.0f));
	int sePercent = static_cast<int>(std::lround(settings.seVolume * 100.0f));
	if (ImGui::SliderInt("BGM音量", &bgmPercent, 0, 100, "%d%%"))
	{
		settings.bgmVolume = static_cast<float>(bgmPercent) / 100.0f;
		settingsChanged = true;
	}
	if (ImGui::SliderInt("SE音量", &sePercent, 0, 100, "%d%%"))
	{
		settings.seVolume = static_cast<float>(sePercent) / 100.0f;
		settingsChanged = true;
	}
	ImGui::TextDisabled("音声実装後に利用する設定値として保存されます");

	ImGui::SeparatorText("演出");
	const bool vibrationWasEnabled = settings.vibrationEnabled;
	settingsChanged |= ImGui::Checkbox(
		"コントローラー振動",
		&settings.vibrationEnabled);
	settingsChanged |= ImGui::Checkbox(
		"画面フラッシュ",
		&settings.screenFlashEnabled);
	settingsChanged |= ImGui::Checkbox(
		"カメラ揺れ",
		&settings.cameraShakeEnabled);
	if (vibrationWasEnabled && !settings.vibrationEnabled)
	{
		Input::SetVibration(0, 0.0f);
	}

	ImGui::SeparatorText("画面");
	settingsChanged |= ImGui::Checkbox(
		"フルスクリーン",
		&settings.fullscreen);
	if (ImGui::BeginCombo(
		"解像度",
		SettingsManager::GetResolutionLabel(settings.resolutionIndex)))
	{
		for (int index = 0;
			index < SettingsManager::GetResolutionCount();
			++index)
		{
			const bool selected = index == settings.resolutionIndex;
			if (ImGui::Selectable(
				SettingsManager::GetResolutionLabel(index),
				selected))
			{
				settings.resolutionIndex = index;
				settingsChanged = true;
			}
			if (selected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	if (ImGui::Button("画面設定を適用", ImVec2(-1.0f, 34.0f)))
	{
		m_PendingDisplayApply = true;
		m_SettingsManager.SaveIfDirty();
	}

	ImGui::SeparatorText("ランを中断");
	if (m_DebugMode)
	{
		if (ImGui::Button("デバッグを終了してタイトルへ", ImVec2(-1, 40))) m_DebugRequest = 2;
	}
	else if (!m_PauseConfirmTitle)
	{
		if (ImGui::Button("セーブしてタイトルへ戻る", ImVec2(-1.0f, 40.0f)))
		{
			m_PauseConfirmTitle = true;
		}
	}
	else
	{
		ImGui::TextWrapped(
			"ランのセーブデータは1個だけです。現在のセーブを上書きしてタイトルへ戻ります。");
		if (dynamic_cast<BattleScene*>(m_Scene) != nullptr &&
			m_GameState != GameState::ClearReward)
		{
			ImGui::TextDisabled("戦闘中のランは、この戦闘の最初から再開します。");
		}
		if (ImGui::Button("上書きして戻る", ImVec2(260.0f, 38.0f)))
		{
			SaveAndReturnToTitle();
		}
		ImGui::SameLine();
		if (ImGui::Button("キャンセル", ImVec2(260.0f, 38.0f)))
		{
			m_PauseConfirmTitle = false;
		}
	}

	if (settingsChanged)
	{
		m_SettingsManager.MarkDirty();
	}
	ImGui::End();
}

// 終了処理
void Game::Uninit()
{
	if (m_Instance != nullptr)
	{
		m_Instance->m_SettingsManager.SaveIfDirty();
		m_Instance->m_GamePresentation.reset();
		if (m_Instance->m_GameMcpBridge != nullptr)
		{
			m_Instance->m_GameMcpBridge->Shutdown(*m_Instance);
		}

		const std::vector<PlayerBall*> players =
			m_Instance->GetComponents<PlayerBall>();
		const std::vector<EnemyBall*> enemies =
			m_Instance->GetComponents<EnemyBall>();
		const int playerHp =
			players.empty() || players[0] == nullptr
			? m_Instance->m_PlayerRunStatus.currentHp
			: players[0]->GetHP();

		BalanceLogger& logger =
			BalanceLogger::GetInstance();
		logger.EndShot(
			playerHp,
			CountAliveEnemies(enemies),
			CountDefeatedEnemies(enemies));
		logger.EndStage(
			"application_exit",
			playerHp,
			m_Instance->m_PlayerRunStatus.maxHp,
			CountDefeatedEnemies(enemies));
		logger.EndRun(
			"application_exit",
			playerHp,
			m_Instance->m_PlayerRunStatus.maxHp,
			m_Instance->m_ClearedStageCount);
	}

	// カメラの終了処理
	m_Instance->m_Camera.Uninit();

	// オブジェクトの終了処理
	for (auto& o : m_Instance->m_GameObjects)
	{
		o->Uninit();
	}


	// 入力処理を終了
	Input::Release();

	// 描画処理を終了
	Renderer::Uninit();

	// インスタンスを削除
	delete m_Instance;
}

// インスタンスを取得
Game* Game::GetInstance()
{
	return m_Instance;
}

GameObject* Game::CreateGameObject(const std::string& name)
{
	auto gameObject = std::make_unique<GameObject>(name);
	GameObject* result = gameObject.get();
	m_GameObjects.emplace_back(std::move(gameObject));
	return result;
}

// シーンを切り替える
void Game::ChangeScene(SceneType sceneType)
{
	ResetFrameTiming();
	int score = 0;

	if (m_Instance->m_Scene != nullptr)
	{
		m_Instance->CaptureCurrentPlayerStatus();
		if (!m_Instance->m_IsRestoringRunSave &&
			(sceneType == SceneType::Select ||
			 sceneType == SceneType::Battle ||
			 sceneType == SceneType::RestSite ||
			 sceneType == SceneType::Shop))
		{
			m_Instance->SaveRunCheckpoint(sceneType, false, false);
		}

		if (BattleScene* battleScene =
			dynamic_cast<BattleScene*>(m_Instance->m_Scene))
		{
			score = battleScene->GetScore();
		}

		delete m_Instance->m_Scene;
		m_Instance->m_Scene = nullptr;
	}

	// =====================================
	// 新しいステージへ入るときに報酬取得状態をリセット
	// =====================================
	if (sceneType == SceneType::Battle)
	{
		// ステージ開始時に現在ボール・山札・捨て札を回収して再シャッフルする。
		m_PlayerDeck.Reset();
		BeginBallSelection();

		m_IsStageRewardCollected = false;
		m_CurrentStageRewardMoney = 0;
		m_RewardMessage.clear();
	}

	switch (sceneType)
	{
	case SceneType::Title:
		if (m_DebugMode) EndDebugMode();
		m_Instance->m_Scene = new TitleScene;
		break;

	case SceneType::Battle:
		m_Instance->m_Scene = new BattleScene;
		break;

	case SceneType::RestSite:
		m_Instance->m_Scene = new RestSiteScene;
		break;

	case SceneType::Shop:
		m_Instance->m_Scene = new ShopScene;
		break;

	case SceneType::Select:
		m_Instance->m_Scene = new StageSelectScene;
		break;

	case SceneType::Result:
		m_Instance->m_Scene = new ResultScene;

		dynamic_cast<ResultScene*>(
			m_Instance->m_Scene
			)->SetScore(score);

		break;

	default:
		break;
	}

	m_Instance->InvalidateDebugCombatForecast("シーン変更");
}

void Game::DeleteGameObject(GameObject* gameObject)
{
	if (ContainsGameObject(gameObject))
	{
		gameObject->Destroy();
	}
}

void Game::RemoveDestroyedGameObjects()
{
	std::erase_if(
		m_GameObjects,
		[](const std::unique_ptr<GameObject>& gameObject) {
			return gameObject != nullptr &&
				gameObject->IsDestroyRequested();
		});

}

// オブジェクトをすべて削除
void Game::DeleteAllGameObjects()
{
	// 終了処理
	for (auto& o : m_Instance->m_GameObjects)
	{
		o->Uninit();
	}
	m_Instance->m_GameObjects.clear();
}

bool Game::ContainsGameObject(const GameObject* gameObject) const
{
	if (gameObject == nullptr) return false;

	for (const auto& ownedGameObject : m_GameObjects)
	{
		if (ownedGameObject.get() == gameObject &&
			!ownedGameObject->IsDestroyRequested())
		{
			return true;
		}
	}

	return false;
}

bool Game::AreAllEnemiesDefeated() const
{
	std::vector<EnemyBall*> enemies = m_Instance->GetComponents<EnemyBall>();

	// 敵が1体もいない場合はクリア扱いにしない
	if (enemies.empty())
	{
		return false;
	}

	for (EnemyBall* enemy : enemies)
	{
		if (enemy == nullptr)
		{
			continue;
		}

		if (!enemy->IsDefeated())
		{
			return false;
		}
	}

	return true;
}

bool Game::AreAllBallsStopped() const
{
	std::vector<GameObject*> balls = m_Instance->GetGameObjectsWith<BallPhysicsComponent>();

	if (balls.empty())
	{
		return false;
	}

	// Game::AreAllBallsStopped()
	for (GameObject* ball : balls)
	{
		if (ball == nullptr) continue;

		// 撃破済みの敵も反射後に停止するまではショット中として扱う。
		BallPhysicsComponent* physics = ball->GetComponent<BallPhysicsComponent>();
		if (physics != nullptr && !physics->IsStopped())
		{
			return false;
		}
	}

	return true;
}

bool Game::TryRecoverClearedBattle(const char* source)
{
	if (dynamic_cast<BattleScene*>(m_Scene) == nullptr ||
		m_GameState == GameState::BallsMoving ||
		m_GameState == GameState::ClearReward ||
		m_GameState == GameState::GameOver ||
		!AreAllEnemiesDefeated())
	{
		return false;
	}

	RecordBalanceEvent(
		"battle_clear_state_recovered",
		{
			{ "source", source != nullptr ? source : "unknown" },
			{ "previous_game_state", GetGameStateDebugName(m_GameState) },
		});
	StartClearReward();
	return true;
}

void Game::ProcessEnemyAttack()
{
	std::vector<PlayerBall*> players = GetComponents<PlayerBall>();
	std::vector<EnemyBall*> enemies = GetComponents<EnemyBall>();

	if (players.empty())
	{
		m_GameState = GameState::GameOver;
		return;
	}

	// 撃破済みの敵はショット中の反射物として残し、
	// 敵の攻撃が始まる直前にだけ取り除く。
	for (EnemyBall* enemy : enemies)
	{
		if (enemy != nullptr && enemy->IsDefeated())
		{
			DeleteGameObject(enemy->GetGameObject());
		}
	}
	enemies = GetComponents<EnemyBall>();

	PlayerBall* player = players[0];
	InvalidateDebugCombatForecast("敵攻撃開始");
	RefreshDebugCombatForecast();
	const int predictedDamage = m_DebugCombatForecast.expectedDamage;
	const int playerHpBeforeEnemyAttack = player->GetHP();

	for (EnemyBall* enemy : enemies)
	{
		if (enemy == nullptr)
		{
			continue;
		}

		if (enemy->IsDefeated())
		{
			continue;
		}
		if (enemy->IsPocketed())
		{
			continue;
		}

		EnemyAttackComponent* attack =
			enemy->GetGameObject()->GetComponent<EnemyAttackComponent>();
		if (attack != nullptr)
		{
			const int hpBefore = player->GetHP();
			attack->Attack(player);
			NotifyPlayerDamage(
				"enemy_attack",
				(std::max)(0, hpBefore - player->GetHP()),
				enemy->GetEnemyId(),
				hpBefore,
				player->GetHP());
		}
	}
	m_DebugLastEnemyAttackComparisonValid = true;
	m_DebugLastEnemyAttackPredictedDamage = predictedDamage;
	m_DebugLastEnemyAttackActualDamage = (std::max)(
		0,
		playerHpBeforeEnemyAttack - player->GetHP());

	CapturePlayerStatusFrom(player);

	if (m_PlayerRunStatus.currentHp <= 0)
	{
		m_GameState = GameState::GameOver;
	}
	else
	{
		RestoreNextPocketedEnemy();
		m_GameState = GameState::TurnEnd;
	}
}

float Game::GetCurrentPocketFinisherRatio() const
{
	switch (m_CurrentBattleStageType)
	{
	case StageType::MidBoss:
		return m_MidBossPocketFinisherRatio;
	case StageType::Boss:
		return m_BossPocketFinisherRatio;
	case StageType::Normal:
	default:
		return m_NormalPocketFinisherRatio;
	}
}

bool Game::IsEnemyPocketFinisherEligible(const EnemyBall* enemy) const
{
    if (enemy && enemy->IsArmorBoss()) return false;
	if (enemy == nullptr || enemy->IsDefeated() || enemy->IsPocketed() ||
		enemy->GetMaxHP() <= 0)
	{
		return false;
	}
	const int pocketDamage = BallMechanics::PocketDamage(enemy->GetMaxHP(), enemy->GetPocketDamageRatio());
	const float hpRatio = static_cast<float>((std::max)(0, enemy->GetHP() - pocketDamage)) /
		static_cast<float>(enemy->GetMaxHP());
	return hpRatio <= GetCurrentPocketFinisherRatio() + 0.0001f;
}

int Game::GetPlayerPocketDamageAmount() const
{
	return (std::max)(1, static_cast<int>(std::ceil(
		static_cast<float>(m_PlayerRunStatus.maxHp) *
		m_PlayerPocketDamageRatio)));
}

void Game::HandleEnemyPocket(EnemyBall* enemy)
{
    if (enemy && enemy->IsArmorBoss()) return;
	if (enemy == nullptr || enemy->IsPocketed())
	{
		return;
	}
	const bool wasAlreadyDefeated = enemy->IsDefeated();
	const int hpBefore = enemy->GetHP();
	const int maxHp = (std::max)(1, enemy->GetMaxHP());
	const int pocketDamage = enemy->ApplyPocketDamage();
	const float hpRatio = static_cast<float>(enemy->GetHP()) /
		static_cast<float>(maxHp);
	if (pocketDamage > 0)
	{
		RecordBalanceEvent("enemy_pocket_damage", {
			{ "enemy_id", enemy->GetEnemyId() }, { "damage", pocketDamage },
			{ "hp_before", hpBefore }, { "hp_after", enemy->GetHP() },
		});
	}
	const float finisherRatio = GetCurrentPocketFinisherRatio();
	const Vector3 pocketPosition = enemy->GetPosition();
	if (wasAlreadyDefeated || enemy->IsDefeated() || hpRatio <= finisherRatio + 0.0001f)
	{
		NotifyPocketFeedback(
			pocketPosition,
			false,
			true,
			hpBefore);
		enemy->Defeat();
		if (!wasAlreadyDefeated && pocketDamage < hpBefore)
		{
			NotifyEnemyDefeated(enemy->GetEnemyId());
		}
		enemy->RemoveFromFieldAfterPocket();
		RecordBalanceEvent(
			"enemy_pocket_finisher",
			{
				{ "enemy_id", enemy->GetEnemyId() },
				{ "stage_type", ToString(m_CurrentBattleStageType) },
				{ "hp_before", hpBefore },
				{ "max_hp", maxHp },
				{ "hp_ratio", hpRatio },
				{ "finisher_ratio", finisherRatio },
				{ "already_defeated", wasAlreadyDefeated },
			});
		return;
	}

	enemy->EnterPocketQueue();
	NotifyPocketFeedback(
		pocketPosition,
		false,
		false,
		pocketDamage);
	m_PocketedEnemyQueue.push_back(enemy);
	RecordBalanceEvent(
		"enemy_pocket_controlled",
		{
			{ "enemy_id", enemy->GetEnemyId() },
			{ "stage_type", ToString(m_CurrentBattleStageType) },
			{ "hp", enemy->GetHP() },
			{ "max_hp", maxHp },
			{ "hp_ratio", hpRatio },
			{ "finisher_ratio", finisherRatio },
			{ "queue_size", m_PocketedEnemyQueue.size() },
		});
}

int Game::GetPocketQueueIndex(const EnemyBall* enemy) const
{
	for (std::size_t index = 0;
		index < m_PocketedEnemyQueue.size();
		index++)
	{
		if (m_PocketedEnemyQueue[index] == enemy)
		{
			return static_cast<int>(index);
		}
	}
	return -1;
}

Vector3 Game::FindPlayerPocketReturnPosition(const PlayerBall* player)
{
	std::uniform_real_distribution<float> xDistribution(
		-m_PlayerPocketReturnHalfWidth,
		m_PlayerPocketReturnHalfWidth);
	std::uniform_real_distribution<float> zDistribution(
		-m_PlayerPocketReturnHalfDepth,
		m_PlayerPocketReturnHalfDepth);
	const float playerRadius = player != nullptr && player->GetBall() != nullptr
		? player->GetBall()->GetRadius()
		: 2.4f;

	for (int attempt = 0; attempt < 24; attempt++)
	{
		const Vector3 candidate(
			xDistribution(m_PocketRandomEngine),
			TableConfig::FIELD_HEIGHT,
			zDistribution(m_PocketRandomEngine));
		bool blocked = false;
		for (BallComponent* ball : GetComponents<BallComponent>())
		{
			if (ball == nullptr || ball->GetGameObject() == nullptr ||
				!ball->GetGameObject()->IsActive() ||
				(player != nullptr && ball == player->GetBall()))
			{
				continue;
			}
			Vector3 difference = candidate - ball->GetPosition();
			difference.y = 0.0f;
			const float clearance = playerRadius + ball->GetRadius() + 0.5f;
			if (difference.LengthSquared() < clearance * clearance)
			{
				blocked = true;
				break;
			}
		}
		if (!blocked)
		{
			return candidate;
		}
	}
	return Vector3(0.0f, TableConfig::FIELD_HEIGHT, 0.0f);
}

void Game::RestorePocketedPlayer()
{
	for (PlayerBall* player : GetComponents<PlayerBall>())
	{
		if (player == nullptr || player->IsDefeated() ||
			!player->IsPocketed())
		{
			continue;
		}

		const Vector3 returnPosition =
			FindPlayerPocketReturnPosition(player);
		player->ReturnFromPocket(returnPosition);
		RecordBalanceEvent(
			"player_pocket_returned",
			{
				{ "position_x", returnPosition.x },
				{ "position_z", returnPosition.z },
			});
	}
}

Vector3 Game::FindEnemyPocketReturnPosition(
	const EnemyBall* returningEnemy) const
{
	const float baseZ = TableConfig::GetFieldDepth() * 0.5f -
		m_EnemyPocketReturnTopEdgeOffset;
	const float radius = returningEnemy != nullptr
		? returningEnemy->GetRadius()
		: 2.4f;
	const float spacing = radius * 2.0f + 1.0f;
	const std::array<int, 9> offsets{ 0, -1, 1, -2, 2, -3, 3, -4, 4 };
	for (int offset : offsets)
	{
		const Vector3 candidate(
			m_EnemyPocketReturnX + static_cast<float>(offset) * spacing,
			TableConfig::FIELD_HEIGHT,
			baseZ);
		bool blocked = false;
		for (BallComponent* ball : m_Instance->GetComponents<BallComponent>())
		{
			if (ball == nullptr || ball->GetGameObject() == nullptr ||
				!ball->GetGameObject()->IsActive() ||
				(returningEnemy != nullptr &&
					ball == returningEnemy->GetBall()))
			{
				continue;
			}
			Vector3 difference = candidate - ball->GetPosition();
			difference.y = 0.0f;
			const float clearance = radius + ball->GetRadius() + 0.5f;
			if (difference.LengthSquared() < clearance * clearance)
			{
				blocked = true;
				break;
			}
		}
		if (!blocked)
		{
			return candidate;
		}
	}
	return Vector3(
		m_EnemyPocketReturnX,
		TableConfig::FIELD_HEIGHT,
		baseZ);
}

void Game::RestoreNextPocketedEnemy()
{
	while (!m_PocketedEnemyQueue.empty())
	{
		EnemyBall* enemy = m_PocketedEnemyQueue.front();
		m_PocketedEnemyQueue.pop_front();
		if (enemy == nullptr || !ContainsComponent(enemy) ||
			enemy->IsDefeated() || !enemy->IsPocketed())
		{
			continue;
		}
		const Vector3 returnPosition =
			FindEnemyPocketReturnPosition(enemy);
		enemy->ReturnFromPocket(returnPosition);
		InvalidateDebugCombatForecast("エネミー復帰");
		RecordBalanceEvent(
			"enemy_pocket_returned",
			{
				{ "enemy_id", enemy->GetEnemyId() },
				{ "position_x", returnPosition.x },
				{ "position_z", returnPosition.z },
				{ "remaining_queue_size", m_PocketedEnemyQueue.size() },
			});
		break;
	}
}

void Game::FinalizeRunResult(bool completed)
{
	std::vector<RelicType> acquiredRelics;
	for (const RelicDefinition& relic : RelicCatalog)
	{
		if (HasRelic(relic.type))
		{
			acquiredRelics.push_back(relic.type);
		}
	}
	m_LastRunResult = m_RunStatistics.CreateSnapshot(
		completed,
		m_PlayerRunStatus.currentHp,
		m_PlayerRunStatus.maxHp,
		acquiredRelics);
	m_LastRunResult.clearedBattles = m_ClearedStageCount;
	RecordPersistentProgress();
}

void Game::RecordPersistentProgress()
{
	m_LastProgressionUnlocks.clear();
	if (!m_PersistentProgressEligible) return;
	try
	{
		m_LastProgressionUnlocks = m_ProgressionProfile.RecordRun(m_LastRunResult, m_ActiveAscension);
		m_ProgressionProfile.Save();
	}
	catch (const std::exception& e)
	{
		m_LastProgressionUnlocks.push_back(std::string("恒久進行を保存できませんでした：") + e.what());
	}
}

void Game::SetSelectedAscension(int level)
{
	if (HasValidRunSave()) return;
	m_ProgressionProfile.selectedAscension = std::clamp(level, 0, m_ProgressionProfile.highestUnlockedAscension);
	try { m_ProgressionProfile.Save(); } catch (...) {}
}

void Game::CompleteNormalRouteArea(const char* areaType)
{
	if (m_RunPhase != RunPhase::NormalRoute)
	{
		return;
	}

	m_RunMap.CompleteActive();
	m_AreaProgress++;
	m_PlayerRunStatus.progress = (std::min)(
		kNormalRouteAreaGoal,
		m_AreaProgress + 1);
	m_RunStatistics.CompleteArea(m_AreaProgress);
	RecordBalanceEvent(
		"route_area_completed",
		{
			{ "map_path", m_RunMap.Path() },
			{ "area_type", areaType != nullptr ? areaType : "unknown" },
			{ "area_progress", m_AreaProgress },
			{ "area_goal", kNormalRouteAreaGoal },
		});

	// 30戦検証は明示的な耐久モードだけで使用し、固定seedなどの
	// 検証設定を有効にしただけでは通常ランの終着点を迂回させない。
	const bool longValidationRun =
		m_BalanceValidationEnabled &&
		m_BalanceValidationEnduranceMode &&
		m_BalanceValidationMaximumClearedStages > kNormalRouteAreaGoal;
	if (longValidationRun && m_AreaProgress >= m_RunMap.StartArea() + m_RunMap.AreaCount())
	{
		m_RunMap.Generate(m_RouteSelectionSeed + static_cast<std::uint32_t>(m_AreaProgress), m_AreaProgress);
		RecordBalanceEvent("run_map_extended", m_RunMap.Snapshot());
	}
	if (!longValidationRun && m_AreaProgress >= kNormalRouteAreaGoal)
	{
		m_AreaProgress = kNormalRouteAreaGoal;
		m_PlayerRunStatus.progress = kNormalRouteAreaGoal;
		m_RunPhase = RunPhase::BossPreparation;
		m_RunMap.Choose(m_RunMap.AreaCount() * 3);
		RecordBalanceEvent(
			"boss_preparation_entered",
			{
				{ "area_progress", m_AreaProgress },
			});
	}
}

void Game::EnterNextRouteAfterArea()
{
	if (m_RunPhase == RunPhase::BossPreparation)
	{
		ChangeScene(SceneType::RestSite);
	}
	else
	{
		ChangeScene(SceneType::Select);
	}
	m_GameState = GameState::AimingDirection;
}

void Game::LeaveShop()
{
	CompleteNormalRouteArea("shop");
	EnterNextRouteAfterArea();
}

void Game::LeaveRestSite()
{
	if (m_RunPhase == RunPhase::BossPreparation)
	{
		m_RunMap.CompleteActive();
		m_RunPhase = RunPhase::FinalBossReady;
		RecordBalanceEvent(
			"boss_preparation_completed",
			{
				{ "area_progress", m_AreaProgress },
				{ "next_phase", ToString(m_RunPhase) },
			});
		ChangeScene(SceneType::Select);
		m_GameState = GameState::AimingDirection;
		return;
	}

	CompleteNormalRouteArea("rest_site");
	EnterNextRouteAfterArea();
}

void Game::ContinueAfterClearReward()
{
	EnterNextRouteAfterArea();
}

void Game::CompleteFinalBossRun()
{
	++m_ClearedStageCount;
	m_RunStatistics.DefeatFinalBoss();
	m_RunMap.CompleteActive();
	m_RunPhase = RunPhase::Completed;
	RecordBalanceEvent(
		"final_boss_defeated",
		{
			{ "stage_id", m_PlayerRunStatus.GetSelectedStageId() },
			{ "area_progress", m_AreaProgress },
			{ "cleared_battle_count", m_ClearedStageCount },
		});
	BalanceLogger::GetInstance().EndRun(
		"completed",
		m_PlayerRunStatus.currentHp,
		m_PlayerRunStatus.maxHp,
		m_ClearedStageCount);
	m_RunActive = false;
	FinalizeRunResult(true);
	GameSaveManager::Remove();
	ChangeScene(SceneType::Result);
	m_GameState = GameState::AimingDirection;
}

void Game::ProcessGameOver()
{
	if (m_DebugMode) { FinishDebugBattle(false); return; }
	DiscardCurrentPlayerBall();

	const std::vector<EnemyBall*> enemies =
		GetComponents<EnemyBall>();
	BalanceLogger& logger = BalanceLogger::GetInstance();
	logger.EndShot(
		m_PlayerRunStatus.currentHp,
		CountAliveEnemies(enemies),
		CountDefeatedEnemies(enemies));
	logger.EndStage(
		"game_over",
		m_PlayerRunStatus.currentHp,
		m_PlayerRunStatus.maxHp,
		CountDefeatedEnemies(enemies));
	EvaluateDynamicBalanceStage(false);
	RecordBalanceEvent(
		"dynamic_balance_evaluation",
		{
			{ "battle_result", "game_over" },
			{ "result", m_DynamicBalanceLastResult },
			{ "reason", m_DynamicBalanceLastReason },
			{ "level_change", m_DynamicBalanceLastLevelChange },
			{ "next_level", m_DynamicBalanceLevel },
			{ "remaining_hp_ratio", m_DynamicBalanceLastHpRatio },
			{ "no_hit_rate", m_DynamicBalanceLastNoHitRate },
			{ "shots_per_enemy", m_DynamicBalanceLastShotsPerEnemy },
		});
	logger.EndRun(
		"game_over",
		m_PlayerRunStatus.currentHp,
		m_PlayerRunStatus.maxHp,
		m_ClearedStageCount);
	m_RunActive = false;
	FinalizeRunResult(false);
	GameSaveManager::Remove();

	ChangeScene(SceneType::Result);
	m_GameState = GameState::AimingDirection;
}

void Game::StartClearReward()
{
	if (m_DebugMode) { FinishDebugBattle(true); return; }
	DiscardCurrentPlayerBall();

	const std::vector<EnemyBall*> enemies =
		GetComponents<EnemyBall>();
	BalanceLogger& logger = BalanceLogger::GetInstance();
	logger.EndShot(
		m_PlayerRunStatus.currentHp,
		CountAliveEnemies(enemies),
		CountDefeatedEnemies(enemies));
	logger.EndStage(
		"clear",
		m_PlayerRunStatus.currentHp,
		m_PlayerRunStatus.maxHp,
		CountDefeatedEnemies(enemies));
	EvaluateDynamicBalanceStage(true);
	RecordBalanceEvent(
		"dynamic_balance_evaluation",
		{
			{ "battle_result", "clear" },
			{ "result", m_DynamicBalanceLastResult },
			{ "reason", m_DynamicBalanceLastReason },
			{ "level_change", m_DynamicBalanceLastLevelChange },
			{ "next_level", m_DynamicBalanceLevel },
			{ "remaining_hp_ratio", m_DynamicBalanceLastHpRatio },
			{ "no_hit_rate", m_DynamicBalanceLastNoHitRate },
			{ "shots_per_enemy", m_DynamicBalanceLastShotsPerEnemy },
		});

	if (m_RunPhase == RunPhase::FinalBoss &&
		m_CurrentBattleStageType == StageType::Boss)
	{
		// 最終ボス後は、以後使えないMoneyや取得物を選ばせない。
		CompleteFinalBossRun();
		return;
	}

	// 敵全滅報酬を所持Moneyへ加算する
	CollectStageRewardMoney();

	// 全滅時は攻撃フェーズへ進まないため、報酬計算後に撃破済みの敵を取り除く。
	for (EnemyBall* enemy : enemies)
	{
		if (enemy != nullptr && enemy->IsDefeated())
		{
			DeleteGameObject(enemy->GetGameObject());
		}
	}

	m_SelectedRewardIndex = 0;
	m_SelectedRewardBallIndex = 0;
	m_IsClearRewardChosen = false;
	m_ClearRewardMouseConfirmed = false;
	m_RewardMessage = UiText::ChooseClearReward;
	m_IsMidBossRelicSelectionActive = false;
	m_MidBossRelicOffers.clear();
	if (m_CurrentBattleStageType == StageType::MidBoss)
	{
		RollMidBossRelicOffers();
	}
	m_ClearedStageCount++;
	if (m_CurrentBattleStageType == StageType::MidBoss)
	{
		m_RunStatistics.DefeatMidBoss();
	}
	CompleteNormalRouteArea(
		m_CurrentBattleStageType == StageType::MidBoss
		? "midboss_battle"
		: "normal_battle");
	if (m_BalanceValidationEnabled &&
		m_BalanceValidationEnduranceMode &&
		m_BalanceValidationMaximumClearedStages > kNormalRouteAreaGoal &&
		m_BalanceValidationMaximumClearedStages > 0 &&
		m_ClearedStageCount >=
			m_BalanceValidationMaximumClearedStages)
	{
		RecordBalanceEvent(
			"balance_validation_run_completed",
			{
				{ "reason", "maximum_cleared_stages_reached" },
				{ "cleared_stage_count", m_ClearedStageCount },
				{ "maximum_cleared_stages",
					m_BalanceValidationMaximumClearedStages },
			});
		logger.EndRun(
			"validation_complete",
			m_PlayerRunStatus.currentHp,
			m_PlayerRunStatus.maxHp,
			m_ClearedStageCount);
		m_RunActive = false;
		FinalizeRunResult(true);
		GameSaveManager::Remove();
		ChangeScene(SceneType::Result);
		m_GameState = GameState::AimingDirection;
		return;
	}
	nlohmann::json newBallCandidates = nlohmann::json::array();
	for (int index = 0; index < m_PlayerDeck.GetCatalogCount(); index++)
	{
		const PlayerBallData* ball = m_PlayerDeck.GetCatalogBall(index);
		if (ball != nullptr)
		{
			newBallCandidates.push_back(
				{
					{ "catalog_index", index },
					{ "ball_id", ball->definitionId },
				});
		}
	}
	nlohmann::json upgradeCandidates = nlohmann::json::array();
	for (int index = 0;
		index < m_PlayerDeck.GetRewardTargetCount();
		index++)
	{
		const PlayerBallData* ball = m_PlayerDeck.GetRewardTarget(index);
		if (ball != nullptr && ball->CanUpgrade())
		{
			const int upgradeCost =
				GetClearRewardUpgradeCost(index);
			upgradeCandidates.push_back(
				{
					{ "deck_index", index },
					{ "instance_id", ball->instanceId },
					{ "ball_id", ball->definitionId },
					{ "upgrade_level", ball->upgradeLevel },
					{ "upgrade_cost", upgradeCost },
					{ "affordable",
						upgradeCost >= 0 &&
						m_PlayerRunStatus.money >= upgradeCost },
				});
		}
	}
	RecordBalanceEvent(
		"clear_reward_offered",
		{
			{
				"reward_types",
				{ "new_ball", "upgrade_ball", "extra_money" }
			},
			{ "new_ball_candidates", std::move(newBallCandidates) },
			{ "upgrade_candidates", std::move(upgradeCandidates) },
			{
				"upgrade_cost_by_current_level",
				{
					{ "0", 15 },
					{ "1", 30 },
				}
			},
			{ "extra_money_amount", kExtraRewardMoney },
		});
	m_GameState = GameState::ClearReward;
}

void Game::CompleteCurrentStage()
{
	if (m_GameState != GameState::ClearReward)
	{
		StartClearReward();
	}
}

StageType Game::GetScheduledStageType() const
{
	if (m_RunPhase == RunPhase::FinalBossReady ||
		m_RunPhase == RunPhase::FinalBoss)
	{
		return StageType::Boss;
	}
	return StageType::Normal;
}

void Game::StartNextBattle()
{
	StartNextBattle(GetScheduledStageType());
}

void Game::StartNextBattle(StageType stageType)
{
	const bool enteringFinalBoss =
		m_RunPhase == RunPhase::FinalBossReady;
	if (enteringFinalBoss)
	{
		stageType = StageType::Boss;
		m_RunPhase = RunPhase::FinalBoss;
	}
	const std::string previousStageId =
		m_PlayerRunStatus.GetSelectedStageId().empty()
		? m_PlayerRunStatus.GetLastStageId()
		: m_PlayerRunStatus.GetSelectedStageId();

	if (m_McpNextStageOverride.has_value())
	{
		m_McpCurrentStageOverride =
			std::move(m_McpNextStageOverride.value());
		m_McpNextStageOverride.reset();
		m_PlayerRunStatus.SetLastStageId(previousStageId);
		m_PlayerRunStatus.SetSelectedStageId(
			m_McpCurrentStageOverride->id);
		if (enteringFinalBoss)
		{
			m_McpCurrentStageOverride->stageType = StageType::Boss;
		}
		ChangeScene(SceneType::Battle);
		return;
	}

	m_McpCurrentStageOverride.reset();
	const std::vector<StageData> stages = StageDataLoader::LoadAll(
		"assets/data/stage_01.json",
		"assets/data/enemy_data.json");

	const StageData* selectedStage = m_StageSelector.SelectStage(
		stages,
		stageType,
		m_PlayerRunStatus.progress,
		previousStageId);
	if (selectedStage == nullptr)
	{
		m_RunMap.CancelActive();
		if (enteringFinalBoss)
		{
			m_RunPhase = RunPhase::FinalBossReady;
		}
		std::cerr << "[Game] 戦闘ステージを選択できなかったため、"
			"シーン遷移を中止します" << std::endl;
		return;
	}

	m_PlayerRunStatus.SetLastStageId(previousStageId);
	m_PlayerRunStatus.SetSelectedStageId(selectedStage->id);
	ChangeScene(SceneType::Battle);
}

void Game::UpdateClearReward()
{
	const bool confirmed = std::exchange(m_ClearRewardMouseConfirmed, false) ||
		Input::GetKeyTrigger(VK_RETURN) || Input::GetKeyTrigger(VK_SPACE);
	if (m_IsMidBossRelicSelectionActive)
	{
		const int offerCount = GetMidBossRelicOfferCount();
		if (offerCount <= 0)
		{
			m_IsMidBossRelicSelectionActive = false;
			return;
		}
		if (Input::GetKeyTrigger(VK_LEFT) || Input::GetKeyTrigger(VK_A) ||
			Input::GetKeyTrigger(VK_UP) || Input::GetKeyTrigger(VK_W))
		{
			m_SelectedRelicOfferIndex =
				(m_SelectedRelicOfferIndex + offerCount - 1) % offerCount;
		}
		if (Input::GetKeyTrigger(VK_RIGHT) || Input::GetKeyTrigger(VK_D) ||
			Input::GetKeyTrigger(VK_DOWN) || Input::GetKeyTrigger(VK_S))
		{
			m_SelectedRelicOfferIndex =
				(m_SelectedRelicOfferIndex + 1) % offerCount;
		}
		if (confirmed)
		{
			AcquireMidBossRelicOffer(m_SelectedRelicOfferIndex);
		}
		return;
	}

	if (m_IsClearRewardChosen)
	{
		if (confirmed)
		{
			ContinueAfterClearReward();
		}
		return;
	}

	if (Input::GetKeyTrigger(VK_UP) || Input::GetKeyTrigger(VK_W))
	{
		m_SelectedRewardIndex =
			(m_SelectedRewardIndex + kClearRewardCount - 1) % kClearRewardCount;
		m_SelectedRewardBallIndex = 0;
	}
	if (Input::GetKeyTrigger(VK_DOWN) || Input::GetKeyTrigger(VK_S))
	{
		m_SelectedRewardIndex = (m_SelectedRewardIndex + 1) % kClearRewardCount;
		m_SelectedRewardBallIndex = 0;
	}

	int targetCount = 0;
	if (m_SelectedRewardIndex == 0)
	{
		targetCount = m_PlayerDeck.GetCatalogCount();
	}
	else if (m_SelectedRewardIndex == 1)
	{
		targetCount = m_PlayerDeck.GetRewardTargetCount();
	}

	if (targetCount > 0)
	{
		if (Input::GetKeyTrigger(VK_LEFT) || Input::GetKeyTrigger(VK_A))
		{
			m_SelectedRewardBallIndex =
				(m_SelectedRewardBallIndex + targetCount - 1) % targetCount;
		}
		if (Input::GetKeyTrigger(VK_RIGHT) || Input::GetKeyTrigger(VK_D))
		{
			m_SelectedRewardBallIndex = (m_SelectedRewardBallIndex + 1) % targetCount;
		}
	}

	if (!confirmed)
	{
		return;
	}

	bool rewardApplied = false;
	nlohmann::json rewardDetails =
	{
		{ "money_before", m_PlayerRunStatus.money },
	};
	switch (m_SelectedRewardIndex)
	{
	case 0:
	{
		std::string acquiredBallId;
		if (const PlayerBallData* selected =
			m_PlayerDeck.GetCatalogBall(m_SelectedRewardBallIndex))
		{
			acquiredBallId = selected->definitionId;
			rewardDetails["reward"] = "new_ball";
			rewardDetails["ball_id"] = selected->definitionId;
		}
		rewardApplied = m_PlayerDeck.AddCatalogBall(m_SelectedRewardBallIndex);
		if (rewardApplied)
		{
			PublishGameEvent(BallAcquiredEvent{ acquiredBallId });
		}
		m_RewardMessage = rewardApplied
			? UiText::NewBallAdded
			: UiText::NoBallAvailable;
		break;
	}
	case 1:
	{
		const PlayerBallData* selected =
			m_PlayerDeck.GetRewardTarget(m_SelectedRewardBallIndex);
		const int upgradeCost =
			GetClearRewardUpgradeCost(m_SelectedRewardBallIndex);
		if (selected != nullptr)
		{
			rewardDetails["reward"] = "upgrade_ball";
			rewardDetails["ball_id"] = selected->definitionId;
			rewardDetails["instance_id"] = selected->instanceId;
			rewardDetails["upgrade_level_before"] = selected->upgradeLevel;
			rewardDetails["upgrade_cost"] = upgradeCost;
		}
		if (upgradeCost < 0)
		{
			m_RewardMessage = UiText::BallAlreadyMax;
		}
		else if (m_PlayerRunStatus.money < upgradeCost)
		{
			m_RewardMessage = UiText::UpgradeMoneyShortage;
		}
		else
		{
			int chargedCost = 0;
			rewardApplied = ApplyClearRewardUpgrade(
				m_SelectedRewardBallIndex,
				chargedCost);
			rewardDetails["upgrade_cost"] = chargedCost;
			m_RewardMessage = rewardApplied
				? UiText::UpgradeApplied
				: UiText::UpgradeUnavailable;
		}
		break;
	}
	case 2:
		rewardDetails["reward"] = "extra_money";
		m_PlayerRunStatus.money += kExtraRewardMoney;
		rewardApplied = true;
		m_RewardMessage = UiText::ExtraMoneyReceived;
		break;
	default:
		break;
	}

	if (rewardApplied)
	{
		rewardDetails["money_after"] = m_PlayerRunStatus.money;
		rewardDetails["controller"] = "human";
		RecordBalanceEvent("clear_reward_choice", rewardDetails);
		m_IsClearRewardChosen = true;
	}
}
void Game::BeginBallSelection()
{
	m_CurrentShotCollisionAttackBonus = 0;
	m_CurrentShotPlayerEnemyCollisionCount = 0;
	m_CurrentShotEnemyEnemyCollisionCount = 0;
	m_CurrentShotBankShotReady = false;
	m_CurrentShotBankShotConsumed = false;
	m_CurrentShotWallCollisionCount = 0;
	m_CurrentShotBounceDamageBonus = 0;
	m_CurrentShotAnchorStopped = false;
	m_CurrentShotLaunchPower = 0.0f;

	if (!m_PlayerDeck.PrepareOffer(GetBallOfferSize()))
	{
		m_GameState = GameState::GameOver;
		return;
	}

	m_SelectedOfferIndex = 0;
	m_SelectedHoldIndex = -1;

	// 前回から保持していたボールは、初期状態では保持を継続する。
	for (int index = 0; index < m_PlayerDeck.GetOfferCount(); index++)
	{
		if (m_PlayerDeck.WasHeldOffer(index))
		{
			m_SelectedHoldIndex = index;
			break;
		}
	}

	if (m_SelectedHoldIndex >= 0 && m_PlayerDeck.GetOfferCount() > 1)
	{
		// 保持中のボールとは別の、新しく引いた候補を初期選択にする。
		m_SelectedOfferIndex = 1;
	}
	else if (m_SelectedHoldIndex == m_SelectedOfferIndex)
	{
		m_SelectedHoldIndex = -1;
	}

	m_GameState = GameState::AimingDirection;
	ApplySelectedBallPreview();
}

void Game::UpdateBallSelection()
{
	const int offerCount = m_PlayerDeck.GetOfferCount();
	if (offerCount <= 0)
	{
		return;
	}

	bool selectionChanged = false;
	for (int index = 0; index < offerCount && index < 4; index++)
	{
		if (Input::GetKeyTrigger('1' + index))
		{
			m_SelectedOfferIndex = index;
			selectionChanged = true;
			if (m_SelectedHoldIndex == index)
			{
				m_SelectedHoldIndex = -1;
			}
		}
	}

	constexpr int HOLD_KEYS[] = { 'Q', 'W', 'E', 'R' };
	for (int index = 0; index < offerCount && index < 4; index++)
	{
		if (!Input::GetKeyTrigger(HOLD_KEYS[index]) ||
			index == m_SelectedOfferIndex)
		{
			continue;
		}

		m_SelectedHoldIndex =
			m_SelectedHoldIndex == index ? -1 : index;
	}

	if (selectionChanged)
	{
		ApplySelectedBallPreview();
	}
}

void Game::ApplySelectedBallPreview()
{
	std::vector<PlayerBall*> players = GetComponents<PlayerBall>();
	if (players.empty() || players[0] == nullptr)
	{
		return;
	}

	ApplyPlayerStatusTo(players[0]);
}

void Game::DrawBallSelectionUI()
{
	GameUi::PrepareWindow("ball_selection", ImVec2(30, 90), ImVec2(560, 500));
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
	ImGui::Begin("ボール選択", nullptr, flags);
	ImGui::TextUnformatted("使用するボールを選んでください。");
	ImGui::TextUnformatted("選択した性能は照準へすぐ反映されます。");
	ImGui::TextUnformatted("残りの候補から1個をホールドできます。");
	ImGui::Separator();

	const int offerCount = m_PlayerDeck.GetOfferCount();
	for (int index = 0; index < offerCount; index++)
	{
		const PlayerBallData* ball = m_PlayerDeck.GetOffer(index);
		if (ball == nullptr)
		{
			continue;
		}

		ImGui::PushID(index);
		if (PlayerBallUI::Select(*ball, m_SelectedOfferIndex == index))
		{
			m_SelectedOfferIndex = index;
			if (m_SelectedHoldIndex == index) m_SelectedHoldIndex = -1;
			ApplySelectedBallPreview();
		}
		ImGui::Text(
			"攻撃:%d  防御:%d  重さ:%.1f  大きさ:%.1f",
			GetEffectivePlayerBallAttack(ball),
			GetEffectivePlayerBallDefense(ball),
			ball->status.mass,
			ball->status.radius);
		ImGui::Text("特性:%s", GetPlayerBallAbilityName(*ball));
		ImGui::TextWrapped("%s", PlayerBallText::GetDescription(ball->definitionId));
		ImGui::TextWrapped("%s", PlayerBallText::GetStats(*ball, ball->status).c_str());

		if (ImGui::RadioButton(
			"使用",
			m_SelectedOfferIndex == index))
		{
			m_SelectedOfferIndex = index;
			if (m_SelectedHoldIndex == index)
			{
				m_SelectedHoldIndex = -1;
			}
			ApplySelectedBallPreview();
		}

		ImGui::SameLine();
		if (m_SelectedOfferIndex == index)
		{
			ImGui::TextUnformatted("このショットで使用");
		}
		else
		{
			const bool isHeld = m_SelectedHoldIndex == index;
			if (ImGui::Button(isHeld ? "ホールド解除" : "ホールド"))
			{
				m_SelectedHoldIndex = isHeld ? -1 : index;
			}
		}

		ImGui::Separator();
		ImGui::PopID();
	}

	ImGui::TextUnformatted(GetBallOfferSize() == 4
		? "1 / 2 / 3 / 4：使用ボールを選択"
		: "1 / 2 / 3：使用ボールを選択");
	ImGui::TextUnformatted(GetBallOfferSize() == 4
		? "Q / W / E / R：ホールドを切り替え"
		: "Q / W / E：ホールドを切り替え");
	ImGui::TextUnformatted("ショットを打つと選択が確定します。");
	ImGui::End();
}

void Game::DrawClearRewardUI()
{
	GameUi::PrepareWindow("clear_reward", ImVec2(290, 90), ImVec2(700, 550));
	ImGui::Begin(UiText::ClearWindow, nullptr, ImGuiWindowFlags_NoCollapse);
	ImGui::TextUnformatted(UiText::StageClear);
	ImGui::Text(UiText::RewardMoneyFormat, m_CurrentStageRewardMoney);
	ImGui::Text(UiText::MoneyFormat, m_PlayerRunStatus.money);
	ImGui::Text("HP %d / %d", m_PlayerRunStatus.currentHp, m_PlayerRunStatus.maxHp);
	ImGui::Separator();
	if (m_IsMidBossRelicSelectionActive)
	{
		ImGui::TextUnformatted("中ボス撃破報酬：レリックを1つ選択");
		for (int index = 0; index < GetMidBossRelicOfferCount(); ++index)
		{
			const auto* relic = GetMidBossRelicOffer(index);
			if (relic == nullptr) continue;
			ImGui::PushID(index);
			if (ImGui::Selectable(relic->name, index == m_SelectedRelicOfferIndex)) m_SelectedRelicOfferIndex = index;
			ImGui::TextWrapped("%s", relic->description);
			ImGui::PopID();
		}
		if (ImGui::Button("選択したレリックを獲得", ImVec2(-1, 40))) m_ClearRewardMouseConfirmed = true;
	}
	else if (m_IsClearRewardChosen)
	{
		ImGui::TextWrapped("%s", m_RewardMessage.c_str());
		if (ImGui::Button("次のルートへ", ImVec2(-1, 44))) m_ClearRewardMouseConfirmed = true;
	}
	else
	{
		ImGui::TextUnformatted(UiText::ChooseReward);
		for (int index = 0; index < kClearRewardCount; ++index)
		{
			if (ImGui::RadioButton(kClearRewardNames[index], index == m_SelectedRewardIndex))
			{
				m_SelectedRewardIndex = index;
				m_SelectedRewardBallIndex = 0;
			}
		}
		ImGui::BeginChild("reward_targets", ImVec2(0, -96), ImGuiChildFlags_Borders);
		const bool upgrading = m_SelectedRewardIndex == 1;
		const int count = m_SelectedRewardIndex == 0 ? m_PlayerDeck.GetCatalogCount() :
			(upgrading ? m_PlayerDeck.GetRewardTargetCount() : 0);
		for (int index = 0; index < count; ++index)
		{
			const auto* ball = upgrading ? m_PlayerDeck.GetRewardTarget(index) : m_PlayerDeck.GetCatalogBall(index);
			if (ball == nullptr) continue;
			ImGui::PushID(index);
			if (PlayerBallUI::Select(*ball, index == m_SelectedRewardBallIndex)) m_SelectedRewardBallIndex = index;
			if (index == m_SelectedRewardBallIndex)
			{
				ImGui::TextWrapped("%s", PlayerBallText::GetDescription(ball->definitionId));
				if (upgrading)
				{
					ImGui::TextWrapped("%s", PlayerBallText::GetUpgradePreview(*ball).c_str());
					const int upgradeCost = GetClearRewardUpgradeCost(index);
					if (upgradeCost >= 0) ImGui::Text(UiText::UpgradeCostFormat, upgradeCost);
					if (upgradeCost > m_PlayerRunStatus.money) ImGui::TextUnformatted(UiText::UpgradeMoneyShortage);
				}
				else ImGui::TextWrapped("%s", PlayerBallText::GetStats(*ball, ball->status).c_str());
			}
			ImGui::Separator();
			ImGui::PopID();
		}
		if (m_SelectedRewardIndex == 2) ImGui::Text("追加で %d Money を受け取ります。", kExtraRewardMoney);
		ImGui::EndChild();
		const int cost = upgrading ? GetClearRewardUpgradeCost(m_SelectedRewardBallIndex) : 0;
		const bool unavailable = upgrading ? cost < 0 || cost > m_PlayerRunStatus.money :
			(m_SelectedRewardIndex == 0 && count == 0);
		ImGui::BeginDisabled(unavailable);
		if (ImGui::Button(upgrading ? "選択した個体を強化" : "選択した報酬を獲得", ImVec2(-1, 40))) m_ClearRewardMouseConfirmed = true;
		ImGui::EndDisabled();
		if (!m_RewardMessage.empty()) ImGui::TextWrapped("%s", m_RewardMessage.c_str());
	}
	ImGui::End();
}
