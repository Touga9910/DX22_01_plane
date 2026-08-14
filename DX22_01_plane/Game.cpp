#include "Game.h"
#include "Renderer.h"
#include "BalanceLogger.h"
#include "GameMcpBridge.h"
#include "BallPhysicsComponent.h"
#include "input.h"

#include "PlayerBall.h"  // DrawImGui呼び出しに必要
#include "EnemyBall.h"   // DrawImGui呼び出しに必要
#include "EnemyAttackComponent.h"
#include "BallComponent.h"
#include "PlayerBallDataLoader.h"
#include "StageDataLoader.h"
#include "EnemyData.h"
#include "TableConfig.h"

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
	constexpr const char* kClearRewardNames[] =
	{
		"New Ball",
		"Upgrade Owned Ball (max +2)",
		"Extra Money (+10)",
	};
	constexpr int kClearRewardCount =
		static_cast<int>(sizeof(kClearRewardNames) / sizeof(kClearRewardNames[0]));
	constexpr int kExtraRewardMoney = 10;
	constexpr int kAutoShopRemoveCost = 15;
	constexpr int kBankShotDamageMultiplier = 2;
	constexpr int kEmergencyRepairContactThreshold = 3;
	constexpr int kEmergencyRepairHealAmount = 1;

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

	BallStatus NormalizeBallStatus(BallStatus status)
	{
		status.maxHp = (std::max)(1, status.maxHp);
		status.mass = (std::max)(0.0001f, status.mass);
		status.radius = (std::max)(0.0f, status.radius);
		status.restitution = std::clamp(status.restitution, 0.0f, 1.0f);
		status.friction = (std::max)(0.0f, status.friction);

		return status;
	}

	PlayerRunStatus NormalizePlayerRunStatus(PlayerRunStatus status)
	{
		status.maxHp = (std::max)(1, status.maxHp);
		status.currentHp = std::clamp(status.currentHp, 0, status.maxHp);
		status.progress = (std::max)(1, status.progress);

		return status;
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

// 初期化
void Game::Init()
{
	// 静的インスタンスをここで1つだけ生成
	if (m_Instance == nullptr) {
		m_Instance = new Game();
	}
	// 描画処理を初期化
	Renderer::Init();

	// 入力処理を初期化
	Input::Create();

	// カメラを初期化
	m_Instance->m_Camera.Init();

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
}

// 更新
void Game::Update()
{
	m_Instance->RemoveDestroyedGameObjects();

	// 入力処理を更新
	Input::Update();

	if (m_Instance->m_GameMcpBridge != nullptr)
	{
		m_Instance->m_GameMcpBridge->Update(*m_Instance);
	}

	if (m_Instance->UpdateBalanceAutoPlay())
	{
		return;
	}

	// ==========================
	// ClearReward中はゲーム本編を更新しない
	// ==========================
	if (m_Instance->m_GameState == GameState::ClearReward)
	{
		m_Instance->UpdateClearReward();
		return;
	}

	if (!m_Instance->m_BalanceAutoPlayEnabled &&
		m_Instance->m_PlayerDeck.GetOfferCount() > 0)
	{
		m_Instance->UpdateBallSelection();
	}

	// シーンを更新
	m_Instance->m_Scene->Update();
	m_Instance->RemoveDestroyedGameObjects();

	// カメラを更新
	m_Instance->m_Camera.Update();

	for (auto& gameObject : m_Instance->m_GameObjects)
	{
		gameObject->FixedUpdate();
	}

	// オブジェクトを更新
	for (auto& gameObject : m_Instance->m_GameObjects)
	{
		gameObject->Update();
	}

	for (auto& gameObject : m_Instance->m_GameObjects)
	{
		gameObject->LateUpdate();
	}
	m_Instance->RemoveDestroyedGameObjects();

	// ゲーム状態を更新（全ボールの停止検知など）
	switch (m_Instance->m_GameState)
	{
	case GameState::BallsMoving:
		if (m_Instance->AreAllBallsStopped())
		{
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

// 描画
void Game::Draw()
{
	Renderer::DrawStart();

	for (auto& gameObject : m_Instance->m_GameObjects)
	{
		gameObject->Draw();
	}

	if (m_Instance->m_Scene != nullptr)
	{
		m_Instance->m_Scene->DrawUI();
	}

	ImGui::Begin("Ball Debugger");

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

	if (ImGui::Button(
		m_Instance->m_BalanceAutoPlayEnabled
			? "Stop Balance Auto Play"
			: "Start Balance Auto Play"))
	{
		m_Instance->m_BalanceAutoPlayEnabled =
			!m_Instance->m_BalanceAutoPlayEnabled;
		m_Instance->m_AutoDecisionFrame = 0;
	}
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

	std::vector<PlayerBall*> players = m_Instance->GetComponents<PlayerBall>();
	for (int i = 0; i < players.size(); i++)
	{
		players[i]->DrawImGui();
	}

	std::vector<EnemyBall*> enemies = m_Instance->GetComponents<EnemyBall>();
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

	if (m_Instance->m_PlayerDeck.GetOfferCount() > 0)
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

	// ImGuiの描画内容を確定
	ImGui::Render();

	// DirectX11でImGuiを描画
	ImGui_ImplDX11_RenderDrawData(
		ImGui::GetDrawData()
	);

	// 最後に画面を表示
	Renderer::DrawEnd();
}

// 終了処理
void Game::Uninit()
{
	if (m_Instance != nullptr)
	{
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
		o->Uninit();;
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
	int score = 0;

	if (m_Instance->m_Scene != nullptr)
	{
		m_Instance->CaptureCurrentPlayerStatus();

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
	const std::size_t removedCount = std::erase_if(
		m_GameObjects,
		[](const std::unique_ptr<GameObject>& gameObject) {
			return gameObject != nullptr &&
				gameObject->IsDestroyRequested();
		});

	if (removedCount > 0)
	{
		m_GameObjects.shrink_to_fit();
	}
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
	m_Instance->m_GameObjects.shrink_to_fit();
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
				enemy->GetEnemyId());
		}
	}

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
	if (enemy == nullptr || enemy->IsDefeated() || enemy->IsPocketed() ||
		enemy->GetMaxHP() <= 0)
	{
		return false;
	}
	const float hpRatio = static_cast<float>(enemy->GetHP()) /
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
	if (enemy == nullptr || enemy->IsPocketed())
	{
		return;
	}
	const bool wasAlreadyDefeated = enemy->IsDefeated();
	const int hpBefore = enemy->GetHP();
	const int maxHp = (std::max)(1, enemy->GetMaxHP());
	const float hpRatio = static_cast<float>(hpBefore) /
		static_cast<float>(maxHp);
	const float finisherRatio = GetCurrentPocketFinisherRatio();
	if (wasAlreadyDefeated || hpRatio <= finisherRatio + 0.0001f)
	{
		enemy->Defeat();
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
	m_PocketedEnemyQueue.push_back(enemy);
	RecordBalanceEvent(
		"enemy_pocket_controlled",
		{
			{ "enemy_id", enemy->GetEnemyId() },
			{ "stage_type", ToString(m_CurrentBattleStageType) },
			{ "hp", hpBefore },
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

void Game::ProcessGameOver()
{
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

	ChangeScene(SceneType::Result);
	m_GameState = GameState::AimingDirection;
}

void Game::StartClearReward()
{
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
	m_RewardMessage = "Choose one clear reward.";
	m_ClearedStageCount++;
	m_PlayerRunStatus.progress = m_ClearedStageCount + 1;
	if (m_RestHealCooldownRemaining > 0)
	{
		m_RestHealCooldownRemaining--;
		RecordBalanceEvent(
			"rest_heal_cooldown_advanced",
			{
				{ "remaining_battles", m_RestHealCooldownRemaining },
				{ "cleared_stage_count", m_ClearedStageCount },
			});
	}
	if (m_BalanceValidationEnabled &&
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
			upgradeCandidates.push_back(
				{
					{ "deck_index", index },
					{ "instance_id", ball->instanceId },
					{ "ball_id", ball->definitionId },
					{ "upgrade_level", ball->upgradeLevel },
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
	const int floor = (std::max)(1, m_PlayerRunStatus.progress);
	if (floor % 10 == 0)
	{
		return StageType::Boss;
	}
	if (floor % 5 == 0)
	{
		return StageType::MidBoss;
	}
	return StageType::Normal;
}

void Game::StartNextBattle()
{
	StartNextBattle(GetScheduledStageType());
}

void Game::StartNextBattle(StageType stageType)
{
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
	if (m_IsClearRewardChosen)
	{
		if (Input::GetKeyTrigger(VK_RETURN) || Input::GetKeyTrigger(VK_SPACE))
		{
			ChangeScene(SceneType::Select);
			m_GameState = GameState::AimingDirection;
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

	if (!Input::GetKeyTrigger(VK_RETURN) && !Input::GetKeyTrigger(VK_SPACE))
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
		if (const PlayerBallData* selected =
			m_PlayerDeck.GetCatalogBall(m_SelectedRewardBallIndex))
		{
			rewardDetails["reward"] = "new_ball";
			rewardDetails["ball_id"] = selected->definitionId;
		}
		rewardApplied = m_PlayerDeck.AddCatalogBall(m_SelectedRewardBallIndex);
		m_RewardMessage = rewardApplied ? "A new ball was added to the deck." : "No ball is available.";
		break;
	case 1:
		if (const PlayerBallData* selected =
			m_PlayerDeck.GetRewardTarget(m_SelectedRewardBallIndex))
		{
			rewardDetails["reward"] = "upgrade_ball";
			rewardDetails["ball_id"] = selected->definitionId;
			rewardDetails["instance_id"] = selected->instanceId;
			rewardDetails["upgrade_level_before"] = selected->upgradeLevel;
		}
		rewardApplied = RestUpgradeBall(m_SelectedRewardBallIndex);
		m_RewardMessage = rewardApplied
			? "The selected ball reached its next upgrade level."
			: "This ball is already +2.";
		break;
	case 2:
		rewardDetails["reward"] = "extra_money";
		m_PlayerRunStatus.money += kExtraRewardMoney;
		rewardApplied = true;
		m_RewardMessage = "Received 10 extra Money.";
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

	if (!m_PlayerDeck.PrepareOffer())
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
	for (int index = 0; index < offerCount && index < 3; index++)
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

	constexpr int HOLD_KEYS[] = { 'Q', 'W', 'E' };
	for (int index = 0; index < offerCount && index < 3; index++)
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
	ImGui::SetNextWindowPos(
		ImVec2(30.0f, 90.0f),
		ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(
		ImVec2(520.0f, 430.0f),
		ImGuiCond_FirstUseEver);

	ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize;

	ImGui::Begin("Ball Selection", nullptr, flags);
	ImGui::TextUnformatted("Choose a ball, then aim and shoot normally.");
	ImGui::TextUnformatted("The selected ball is applied immediately.");
	ImGui::TextUnformatted("You may hold one of the other balls.");
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
		ImGui::Text(
			"[%d] %s%s",
			index + 1,
			ball->definitionId.c_str(),
			m_PlayerDeck.WasHeldOffer(index) ? "  (HELD)" : "");
		ImGui::Text(
			"ATK:%d  DEF:%d  MASS:%.2f  RADIUS:%.2f",
			GetEffectivePlayerBallAttack(ball),
			GetEffectivePlayerBallDefense(ball),
			ball->status.mass,
			ball->status.radius);
		ImGui::Text(
			"Pierce:%s  Anchor:%s",
			ball->status.abilities.pierce ? "Yes" : "No",
			ball->status.abilities.anchor ? "Yes" : "No");

		if (ImGui::RadioButton(
			"Use",
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
			ImGui::TextUnformatted("Selected for this shot");
		}
		else
		{
			const bool isHeld = m_SelectedHoldIndex == index;
			if (ImGui::Button(isHeld ? "Release Hold" : "Hold"))
			{
				m_SelectedHoldIndex = isHeld ? -1 : index;
			}
		}

		ImGui::Separator();
		ImGui::PopID();
	}

	ImGui::TextUnformatted("1 / 2 / 3 : Use ball");
	ImGui::TextUnformatted("Q / W / E : Toggle hold");
	ImGui::TextUnformatted("The choice is finalized when the shot is fired.");
	ImGui::End();
}

void Game::DrawClearRewardUI()
{
	ImGui::SetNextWindowPos(ImVec2(290.0f, 90.0f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(700.0f, 540.0f), ImGuiCond_Always);
	const ImGuiWindowFlags clearFlags = ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
	ImGui::Begin("Stage Clear", nullptr, clearFlags);
	ImGui::TextUnformatted("STAGE CLEAR!");
	ImGui::Separator();
	ImGui::Text("Reward : +%d Money", m_CurrentStageRewardMoney);
	ImGui::Text("Money : %d", m_PlayerRunStatus.money);
	ImGui::Text("HP : %d / %d", m_PlayerRunStatus.currentHp, m_PlayerRunStatus.maxHp);
	ImGui::Text("Cleared stages : %d", m_ClearedStageCount);
	ImGui::Separator();

	if (!m_IsClearRewardChosen)
	{
		ImGui::TextUnformatted("Choose one reward");
		for (int index = 0; index < kClearRewardCount; index++)
		{
			ImGui::Text("%s %s", index == m_SelectedRewardIndex ? ">" : " ", kClearRewardNames[index]);
		}

		if (m_SelectedRewardIndex == 0)
		{
			ImGui::Separator();
			ImGui::TextUnformatted("New ball (LEFT / RIGHT)");
			for (int index = 0; index < m_PlayerDeck.GetCatalogCount(); index++)
			{
				const PlayerBallData* ball = m_PlayerDeck.GetCatalogBall(index);
				if (ball != nullptr)
				{
					ImGui::Text("%s %s  ATK:%d DEF:%d",
						index == m_SelectedRewardBallIndex ? ">" : " ",
						ball->definitionId.c_str(),
						GetEffectivePlayerBallAttack(ball),
						GetEffectivePlayerBallDefense(ball));
				}
			}
		}
		else if (m_SelectedRewardIndex == 1)
		{
			ImGui::Separator();
			ImGui::TextUnformatted("Ball to upgrade (LEFT / RIGHT)");
			for (int index = 0; index < m_PlayerDeck.GetRewardTargetCount(); index++)
			{
				const PlayerBallData* ball = m_PlayerDeck.GetRewardTarget(index);
				if (ball != nullptr)
				{
					ImGui::Text("%s [%d] %s  +%d  ATK:%d DEF:%d",
						index == m_SelectedRewardBallIndex ? ">" : " ", index,
						ball->definitionId.c_str(), ball->upgradeLevel,
						GetEffectivePlayerBallAttack(ball),
						GetEffectivePlayerBallDefense(ball));
					if (ball->CanUpgrade())
					{
						const BallUpgradeStep& next = ball->upgradeTable[ball->upgradeLevel];
						ImGui::Text("    Next: ATK:%d DEF:%d", next.attack, next.defense);
					}
					else
					{
						ImGui::TextUnformatted("    MAX +2");
					}
				}
			}
		}

		ImGui::Separator();
		ImGui::TextUnformatted("UP/DOWN : Reward    LEFT/RIGHT : Ball    ENTER/SPACE : Claim");
	}
	else
	{
		ImGui::TextUnformatted(m_RewardMessage.c_str());
		ImGui::Separator();
		ImGui::TextUnformatted("ENTER or SPACE : Choose the next route");
	}
	ImGui::End();
}
void Game::LoadPlayerStatusFromJson(
	const std::string& filePath,
	const std::string& deckFilePath)
{
	PlayerBallDataLoadResult loadResult =
		PlayerBallDataLoader::Load(
			filePath,
			m_DefaultPlayerStatus,
			m_DefaultPlayerRunStatus
		);

	m_DefaultPlayerStatus = loadResult.defaultBallStatus;
	m_DefaultPlayerRunStatus = NormalizePlayerRunStatus(
		loadResult.defaultRunStatus
	);
	m_RestHealRatio = loadResult.restHealRatio;
	m_RestHealCooldownBattles = loadResult.restHealCooldownBattles;
	m_PlayerDeck.SetCatalog(loadResult.ballDefinitions);

	std::vector<PlayerBallData> defaultDeck =
		PlayerBallDataLoader::LoadDeck(
			deckFilePath,
			loadResult.ballDefinitions
		);
	m_PlayerDeck.SetDefaultDeck(defaultDeck);

	ResetPlayerRuntimeStatus();
}
void Game::ResetPlayerRuntimeStatus()
{
	m_PlayerRunStatus = NormalizePlayerRunStatus(m_DefaultPlayerRunStatus);
	m_PlayerRunStatus.progress = 1;
	m_PlayerRunStatus.SetSelectedStageId("");
	m_PlayerRunStatus.SetLastStageId("");
	m_PlayerDeck.ResetToDefault();
	m_OwnedRelics.fill(false);
	m_CurrentShotCollisionAttackBonus = 0;
	m_CurrentShotPlayerEnemyCollisionCount = 0;
	m_CurrentShotEnemyEnemyCollisionCount = 0;
	m_ClearedStageCount = 0;
	m_RestHealCooldownRemaining = 0;
	m_AutoPendingBallAdjustments.clear();
	m_PocketedEnemyQueue.clear();
	m_McpCurrentStageOverride.reset();
}

void Game::ResetDynamicBalanceRunState()
{
	m_DynamicBalanceEnabled = m_DynamicBalanceConfiguredEnabled;
	m_DynamicBalanceLevel = std::clamp(
		m_DynamicBalanceInitialLevel,
		m_DynamicBalanceMinLevel,
		m_DynamicBalanceMaxLevel);
	m_DynamicBalanceAppliedEnabled = false;
	m_DynamicBalanceAppliedLevel = m_DynamicBalanceLevel;
	m_DynamicBalanceStageActive = false;
	m_DynamicBalanceShotActive = false;
	m_DynamicBalanceCurrentShotHit = false;
	m_DynamicBalanceStageShots = 0;
	m_DynamicBalanceStageNoHitShots = 0;
	m_DynamicBalanceStageEnemyCount = 0;
	m_DynamicBalanceLastLevelChange = 0;
	m_DynamicBalanceLastHpRatio = 1.0f;
	m_DynamicBalanceLastNoHitRate = 0.0f;
	m_DynamicBalanceLastShotsPerEnemy = 0.0f;
	m_DynamicBalanceLastResult = "not_evaluated";
	m_DynamicBalanceLastReason = "No battle has been evaluated in this run.";
}

void Game::StartNewRun(
	const std::string& controllerType,
	const std::string& controllerProfile)
{
	ResetPlayerRuntimeStatus();
	ResetDynamicBalanceRunState();
	m_PendingShotTelemetry = nlohmann::json::object();
	if (m_BalanceValidationEnabled)
	{
		const std::uint32_t seedCount =
			static_cast<std::uint32_t>(
				(std::max)(std::size_t{ 1 },
					m_BalanceValidationSeeds.size()));
		const std::uint32_t variantCount =
			static_cast<std::uint32_t>(
				(std::max)(std::size_t{ 1 },
					m_BalanceValidationVariants.size()));
		m_BalanceValidationSeedIndex =
			m_BalanceValidationSeeds.empty()
			? 0
			: m_BalanceValidationRunCounter % seedCount;
		m_BalanceValidationVariantIndex =
			(m_BalanceValidationRunCounter / seedCount) % variantCount;
		if (!m_BalanceValidationVariants.empty())
		{
			const BalanceValidationVariant& variant =
				m_BalanceValidationVariants[
					m_BalanceValidationVariantIndex];
			m_BalanceValidationCurrentVariantId = variant.id;
			m_BalanceValidationCurrentDisableDynamicBalance =
				variant.disableDynamicBalance;
		}
		else
		{
			m_BalanceValidationCurrentVariantId =
				m_BalanceValidationExperimentId;
			m_BalanceValidationCurrentDisableDynamicBalance =
				m_BalanceValidationDisableDynamicBalance;
		}
		m_RunRandomSeed = m_BalanceValidationSeeds.empty()
			? m_BalanceValidationSeed
			: m_BalanceValidationSeeds[m_BalanceValidationSeedIndex];
		m_BalanceValidationRunCounter++;
		if (m_BalanceValidationCurrentDisableDynamicBalance)
		{
			m_DynamicBalanceEnabled = false;
			m_DynamicBalanceAppliedEnabled = false;
			m_DynamicBalanceLevel = 0;
			m_DynamicBalanceAppliedLevel = 0;
		}
	}
	else if (m_BalanceAutoPlayEnabled)
	{
		m_RunRandomSeed =
			m_AutoRandomSeed + static_cast<std::uint32_t>(m_AutoRunCount);
	}
	else
	{
		m_RunRandomSeed = std::random_device{}();
	}
	m_StageSelectionSeed = m_RunRandomSeed ^ 0x9e3779b9u;
	m_RouteSelectionSeed = m_RunRandomSeed ^ 0x85ebca6bu;
	m_RouteSelectionCounter = 0;
	m_StageSelector.Seed(m_StageSelectionSeed);
	m_AutoRandomEngine.seed(m_RunRandomSeed ^ 0xc2b2ae35u);
	m_PocketRandomEngine.seed(m_RunRandomSeed ^ 0x27d4eb2fu);

	std::vector<BalanceBallSnapshot> deck;
	deck.reserve(static_cast<size_t>(
		m_PlayerDeck.GetRewardTargetCount()));

	for (int index = 0;
		index < m_PlayerDeck.GetRewardTargetCount();
		index++)
	{
		const PlayerBallData* ball =
			m_PlayerDeck.GetRewardTarget(index);
		if (ball == nullptr)
		{
			continue;
		}

		BalanceBallSnapshot snapshot;
		snapshot.id = ball->definitionId;
		snapshot.instanceId = ball->instanceId;
		snapshot.upgradeLevel = ball->upgradeLevel;
		snapshot.attack = ball->status.attack;
		snapshot.defense = ball->status.defense;
		snapshot.mass = ball->status.mass;
		snapshot.radius = ball->status.radius;
		snapshot.restitution = ball->status.restitution;
		snapshot.friction = ball->status.friction;
		snapshot.split = ball->status.abilities.split;
		snapshot.pierce = ball->status.abilities.pierce;
		snapshot.anchor = ball->status.abilities.anchor;
		deck.push_back(std::move(snapshot));
	}

	const std::string effectiveControllerType =
		!controllerType.empty()
		? controllerType
		: (m_BalanceAutoPlayEnabled ? "autoplay" : "human");
	const nlohmann::json runContext =
	{
		{ "controller_profile", controllerProfile },
		{
			"randomness",
			{
				{ "run_seed", m_RunRandomSeed },
				{ "autoplay_seed", m_RunRandomSeed ^ 0xc2b2ae35u },
				{ "stage_selection_seed", m_StageSelectionSeed },
				{ "route_selection_seed", m_RouteSelectionSeed },
			}
		},
		{
			"validation",
			{
				{ "enabled", m_BalanceValidationEnabled },
				{ "experiment_id", m_BalanceValidationExperimentId },
				{ "variant_id", m_BalanceValidationCurrentVariantId },
				{ "variant_index", m_BalanceValidationVariantIndex },
				{ "variant_count", m_BalanceValidationVariants.size() },
				{ "maximum_cleared_stages",
					m_BalanceValidationMaximumClearedStages },
				{
					"dynamic_balance_forced_off",
					m_BalanceValidationEnabled &&
					m_BalanceValidationCurrentDisableDynamicBalance
				},
				{ "fixed_stage_schedule", m_BalanceValidationFixedStageSchedule },
				{ "seed_suite_index", m_BalanceValidationSeedIndex },
				{ "seed_suite_size", m_BalanceValidationSeeds.size() },
			}
		},
		{
			"baseline_difficulty",
			{
				{ "profile", m_BaselineDifficultyProfile },
				{ "enemy_hp_multiplier", m_BaselineEnemyHpMultiplier },
				{ "enemy_attack_delta", m_BaselineEnemyAttackDelta },
			}
		},
		{ "dynamic_balance_enabled_at_start", m_DynamicBalanceEnabled },
		{ "dynamic_balance_level_at_start", m_DynamicBalanceLevel },
		{ "initial_money", m_PlayerRunStatus.money },
		{ "initial_progress", m_PlayerRunStatus.progress },
	};

	BalanceLogger::GetInstance().BeginRun(
		m_PlayerRunStatus.maxHp,
		m_PlayerRunStatus.currentHp,
		deck,
		effectiveControllerType,
		runContext);
}

void Game::LoadBalanceAutoPlayConfig(
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

		m_BalanceAutoPlayEnabled =
			config.value("enabled", false);
		m_AutoRestartAfterGameOver =
			config.value("restart_after_game_over", true);
		m_AutoDecisionDelayFrames =
			(std::max)(
				1,
				config.value("decision_delay_frames", 20));
		m_AutoMaxRuns =
			(std::max)(0, config.value("max_runs", 0));
		m_AutoMinShotPower =
			std::clamp(
				config.value("min_shot_power", 4.0f),
				1.0f,
				8.0f);
		m_AutoMaxShotPower =
			std::clamp(
				config.value("max_shot_power", 8.0f),
				m_AutoMinShotPower,
				8.0f);
		m_AutoAimJitterDegrees =
			std::clamp(
				config.value("aim_jitter_degrees", 1.5f),
				0.0f,
				15.0f);

		const unsigned int randomSeed =
			config.value("random_seed", 20260727u);
		m_AutoRandomSeed = randomSeed;
		m_AutoRandomEngine.seed(randomSeed);

		std::cout
			<< "[BalanceAutoPlay] "
			<< (m_BalanceAutoPlayEnabled ? "Enabled" : "Disabled")
			<< " / F8 toggles auto play"
			<< std::endl;
	}
	catch (const nlohmann::json::exception& error)
	{
		std::cerr
			<< "[BalanceAutoPlay] Invalid config: "
			<< error.what() << std::endl;
	}
}

void Game::LoadDynamicBalanceConfig(
	const std::string& filePath)
{
	std::ifstream file(filePath);
	if (!file)
	{
		std::cout
			<< "[DynamicBalance] Config not found: "
			<< filePath << std::endl;
		return;
	}

	try
	{
		nlohmann::json config;
		file >> config;

		m_DynamicBalanceConfiguredEnabled =
			config.value(
				"enabled",
				m_DynamicBalanceConfiguredEnabled);
		m_DynamicBalanceEnabled =
			m_DynamicBalanceConfiguredEnabled;
		m_DynamicBalanceMinLevel =
			config.value("minimum_level", m_DynamicBalanceMinLevel);
		m_DynamicBalanceMaxLevel =
			config.value("maximum_level", m_DynamicBalanceMaxLevel);
		if (m_DynamicBalanceMinLevel > m_DynamicBalanceMaxLevel)
		{
			std::swap(
				m_DynamicBalanceMinLevel,
				m_DynamicBalanceMaxLevel);
		}
		m_DynamicBalanceInitialLevel = std::clamp(
			config.value(
				"initial_level",
				m_DynamicBalanceInitialLevel),
			m_DynamicBalanceMinLevel,
			m_DynamicBalanceMaxLevel);
		m_DynamicBalanceLevel = m_DynamicBalanceInitialLevel;
		m_DynamicBalanceHpStep =
			(std::max)(0, config.value(
				"hp_step_per_level",
				m_DynamicBalanceHpStep));
		m_DynamicBalanceAttackStep =
			(std::max)(0, config.value(
				"attack_step",
				m_DynamicBalanceAttackStep));
		m_DynamicBalanceLevelsPerAttackStep =
			(std::max)(1, config.value(
				"levels_per_attack_step",
				m_DynamicBalanceLevelsPerAttackStep));
		m_DynamicBalancePositiveAttackScalingEnabled =
			config.value(
				"positive_attack_scaling_enabled",
				m_DynamicBalancePositiveAttackScalingEnabled);
		const nlohmann::json progression = config.value(
			"progression_scaling",
			nlohmann::json::object());
		if (progression.is_object())
		{
			m_ProgressionScalingEnabled = progression.value(
				"enabled",
				m_ProgressionScalingEnabled);
			m_ProgressionAttackStart = (std::max)(
				1,
				progression.value(
					"attack_start_progress",
					m_ProgressionAttackStart));
			m_ProgressionAttackInterval = (std::max)(
				1,
				progression.value(
					"attack_interval",
					m_ProgressionAttackInterval));
			m_ProgressionAttackStep = (std::max)(
				0,
				progression.value(
					"attack_step",
					m_ProgressionAttackStep));
			m_ProgressionAttackMaximumDelta = (std::max)(
				0,
				progression.value(
					"maximum_attack_delta",
					m_ProgressionAttackMaximumDelta));
		}
		m_DynamicBalanceMinEnemyHp =
			(std::max)(1, config.value(
				"minimum_enemy_hp",
				m_DynamicBalanceMinEnemyHp));
		m_DynamicBalanceMaxEnemyHp =
			(std::max)(
				m_DynamicBalanceMinEnemyHp,
				config.value(
					"maximum_enemy_hp",
					m_DynamicBalanceMaxEnemyHp));
		m_DynamicBalanceMinEnemyAttack =
			(std::max)(0, config.value(
				"minimum_enemy_attack",
				m_DynamicBalanceMinEnemyAttack));
		m_DynamicBalanceMaxEnemyAttack =
			(std::max)(
				m_DynamicBalanceMinEnemyAttack,
				config.value(
					"maximum_enemy_attack",
					m_DynamicBalanceMaxEnemyAttack));
		m_DynamicBalanceStrongHpRatio = std::clamp(
			config.value(
				"strong_clear_hp_ratio",
				m_DynamicBalanceStrongHpRatio),
			0.0f,
			1.0f);
		m_DynamicBalanceWeakHpRatio = std::clamp(
			config.value(
				"weak_clear_hp_ratio",
				m_DynamicBalanceWeakHpRatio),
			0.0f,
			1.0f);
		m_DynamicBalanceStrongNoHitRate = std::clamp(
			config.value(
				"strong_no_hit_rate",
				m_DynamicBalanceStrongNoHitRate),
			0.0f,
			1.0f);
		m_DynamicBalanceWeakNoHitRate = std::clamp(
			config.value(
				"weak_no_hit_rate",
				m_DynamicBalanceWeakNoHitRate),
			0.0f,
			1.0f);
		m_DynamicBalanceTargetShotsPerEnemy =
			(std::max)(
				0.1f,
				config.value(
					"target_shots_per_enemy",
					m_DynamicBalanceTargetShotsPerEnemy));
		m_DynamicBalanceWeakShotMultiplier =
			(std::max)(
				1.0f,
				config.value(
					"weak_shot_multiplier",
					m_DynamicBalanceWeakShotMultiplier));

		std::cout
			<< "[DynamicBalance] "
			<< (m_DynamicBalanceEnabled ? "Enabled" : "Disabled")
			<< " / Level=" << m_DynamicBalanceLevel
			<< " / Range=" << m_DynamicBalanceMinLevel
			<< ".." << m_DynamicBalanceMaxLevel
			<< std::endl;
	}
	catch (const nlohmann::json::exception& error)
	{
		std::cerr
			<< "[DynamicBalance] Invalid config: "
			<< error.what() << std::endl;
	}
}

void Game::LoadDifficultyProfileConfig(
	const std::string& filePath)
{
	std::ifstream file(filePath);
	if (!file)
	{
		std::cout << "[DifficultyProfile] Config not found: "
			<< filePath << std::endl;
		return;
	}

	try
	{
		nlohmann::json config;
		file >> config;
		const std::string selected = config.value(
			"selected_profile",
			std::string("normal"));
		const nlohmann::json& profiles = config.at("profiles");
		if (!profiles.contains(selected) ||
			!profiles[selected].is_object())
		{
			throw std::runtime_error(
				"selected_profile does not exist in profiles");
		}
		const nlohmann::json& profile = profiles[selected];
		m_BaselineDifficultyProfile = selected;
		m_BaselineEnemyHpMultiplier = std::clamp(
			profile.value("enemy_hp_multiplier", 1.0f),
			0.25f,
			4.0f);
		m_BaselineEnemyAttackDelta = std::clamp(
			profile.value("enemy_attack_delta", 0),
			-10,
			10);
		std::cout << "[DifficultyProfile] " << selected
			<< " / HP x" << m_BaselineEnemyHpMultiplier
			<< " / ATK " << m_BaselineEnemyAttackDelta
			<< std::endl;
	}
	catch (const std::exception& error)
	{
		std::cerr << "[DifficultyProfile] Invalid config: "
			<< error.what() << std::endl;
	}
}

void Game::LoadBalanceValidationConfig(
	const std::string& filePath)
{
	std::ifstream file(filePath);
	if (!file)
	{
		std::cout << "[BalanceValidation] Config not found: "
			<< filePath << std::endl;
		return;
	}

	try
	{
		nlohmann::json config;
		file >> config;
		m_BalanceValidationEnabled =
			config.value("enabled", false);
		m_BalanceValidationDisableDynamicBalance =
			config.value("disable_dynamic_balance", true);
		m_BalanceValidationFixedStageSchedule =
			config.value("fixed_stage_schedule", true);
		m_BalanceValidationSeed =
			config.value("random_seed", 20260807u);
		m_BalanceValidationSeeds.clear();
		if (config.contains("random_seeds") &&
			config["random_seeds"].is_array())
		{
			for (const nlohmann::json& seed : config["random_seeds"])
			{
				if (seed.is_number_unsigned() || seed.is_number_integer())
				{
					const long long value = seed.get<long long>();
					if (value >= 0 && value <= 0xffffffffll)
					{
						m_BalanceValidationSeeds.push_back(
							static_cast<std::uint32_t>(value));
					}
				}
			}
		}
		if (m_BalanceValidationSeeds.empty())
		{
			m_BalanceValidationSeeds.push_back(
				m_BalanceValidationSeed);
		}
		m_BalanceValidationExperimentId = config.value(
			"experiment_id",
			std::string("fixed_baseline"));
		m_BalanceValidationMaximumClearedStages = (std::max)(
			0,
			config.value(
				"maximum_cleared_stages_per_run",
				m_BalanceValidationMaximumClearedStages));
		m_BalanceValidationVariants.clear();
		if (config.contains("variants") &&
			config["variants"].is_array())
		{
			for (const nlohmann::json& variant : config["variants"])
			{
				if (!variant.is_object())
				{
					continue;
				}
				const std::string id = variant.value(
					"id",
					std::string());
				if (id.empty())
				{
					continue;
				}
				m_BalanceValidationVariants.push_back({
					id,
					variant.value(
						"disable_dynamic_balance",
						m_BalanceValidationDisableDynamicBalance),
				});
			}
		}
		if (m_BalanceValidationVariants.empty())
		{
			m_BalanceValidationVariants.push_back({
				m_BalanceValidationExperimentId,
				m_BalanceValidationDisableDynamicBalance,
			});
		}
		m_BalanceValidationVariantIndex = 0;
		m_BalanceValidationCurrentVariantId =
			m_BalanceValidationVariants.front().id;
		m_BalanceValidationCurrentDisableDynamicBalance =
			m_BalanceValidationVariants.front().disableDynamicBalance;
		if (m_BalanceValidationEnabled &&
			config.contains("baseline_profile") &&
			config["baseline_profile"].is_string())
		{
			const std::string requestedProfile =
				config["baseline_profile"].get<std::string>();
			if (requestedProfile != m_BaselineDifficultyProfile)
			{
				std::cout
					<< "[BalanceValidation] baseline_profile is "
					<< requestedProfile
					<< "; set the same selected_profile in "
					<< "difficulty_profiles.json to apply it."
					<< std::endl;
			}
		}
		std::cout << "[BalanceValidation] "
			<< (m_BalanceValidationEnabled ? "Enabled" : "Disabled")
			<< " / Seed=" << m_BalanceValidationSeed
			<< " / Experiment=" << m_BalanceValidationExperimentId
			<< " / Variants=" << m_BalanceValidationVariants.size()
			<< std::endl;
	}
	catch (const nlohmann::json::exception& error)
	{
		std::cerr << "[BalanceValidation] Invalid config: "
			<< error.what() << std::endl;
	}
}

void Game::LoadEncounterBalanceConfig(
	const std::string& filePath)
{
	std::ifstream file(filePath);
	if (!file)
	{
		std::cout << "[EncounterBalance] Config not found: "
			<< filePath << std::endl;
		return;
	}

	try
	{
		nlohmann::json config;
		file >> config;
		m_EnemyThreatCosts.clear();
		m_StageThreatTargets.clear();
		const nlohmann::json enemyCosts = config.value(
			"enemy_costs",
			nlohmann::json::object());
		for (auto iterator = enemyCosts.begin();
			iterator != enemyCosts.end(); ++iterator)
		{
			if (iterator.value().is_number())
			{
				m_EnemyThreatCosts[iterator.key()] =
					(std::max)(0.0f, iterator.value().get<float>());
			}
		}
		const nlohmann::json stageTargets = config.value(
			"stage_target_budgets",
			nlohmann::json::object());
		for (auto iterator = stageTargets.begin();
			iterator != stageTargets.end(); ++iterator)
		{
			if (iterator.value().is_number())
			{
				m_StageThreatTargets[iterator.key()] =
					(std::max)(0.0f, iterator.value().get<float>());
			}
		}
		const nlohmann::json layouts = config.value(
			"layout_multipliers",
			nlohmann::json::object());
		m_StageDataLayoutThreatMultiplier =
			(std::max)(0.1f, layouts.value("stage_data", 1.0f));
		m_DenseLayoutThreatMultiplier =
			(std::max)(0.1f, layouts.value("dense_auto_layout", 1.25f));
		m_McpLayoutThreatMultiplier =
			(std::max)(0.1f, layouts.value("mcp_override", 1.0f));
	}
	catch (const nlohmann::json::exception& error)
	{
		std::cerr << "[EncounterBalance] Invalid config: "
			<< error.what() << std::endl;
	}
}

void Game::LoadPocketRulesConfig(const std::string& filePath)
{
	std::ifstream file(filePath);
	if (!file)
	{
		std::cout << "[PocketRules] Config not found: "
			<< filePath << std::endl;
		return;
	}

	try
	{
		nlohmann::json config;
		file >> config;
		m_PlayerPocketDamageRatio = std::clamp(
			config.value("player_max_hp_damage_ratio",
				m_PlayerPocketDamageRatio),
			0.0f,
			1.0f);
		const nlohmann::json finishers = config.value(
			"enemy_finisher_hp_ratios",
			nlohmann::json::object());
		m_NormalPocketFinisherRatio = std::clamp(
			finishers.value("normal", m_NormalPocketFinisherRatio),
			0.0f,
			1.0f);
		m_MidBossPocketFinisherRatio = std::clamp(
			finishers.value("midboss", m_MidBossPocketFinisherRatio),
			0.0f,
			1.0f);
		m_BossPocketFinisherRatio = std::clamp(
			finishers.value("boss", m_BossPocketFinisherRatio),
			0.0f,
			1.0f);
		const nlohmann::json playerReturn = config.value(
			"player_return_region",
			nlohmann::json::object());
		m_PlayerPocketReturnHalfWidth = (std::max)(
			0.0f,
			playerReturn.value(
				"half_width", m_PlayerPocketReturnHalfWidth));
		m_PlayerPocketReturnHalfDepth = (std::max)(
			0.0f,
			playerReturn.value(
				"half_depth", m_PlayerPocketReturnHalfDepth));
		const nlohmann::json enemyReturn = config.value(
			"enemy_return",
			nlohmann::json::object());
		const nlohmann::json enemyReturnPosition = enemyReturn.value(
			"position",
			nlohmann::json::object());
		m_EnemyPocketReturnX = enemyReturnPosition.value(
			"x", m_EnemyPocketReturnX);
		m_EnemyPocketReturnTopEdgeOffset = (std::max)(
			0.0f,
			enemyReturnPosition.value(
				"z_from_top_edge",
				m_EnemyPocketReturnTopEdgeOffset));
	}
	catch (const nlohmann::json::exception& error)
	{
		std::cerr << "[PocketRules] Invalid config: "
			<< error.what() << std::endl;
	}
}

void Game::FinishDynamicBalanceShot()
{
	if (!m_DynamicBalanceStageActive ||
		!m_DynamicBalanceShotActive)
	{
		return;
	}

	if (!m_DynamicBalanceCurrentShotHit)
	{
		m_DynamicBalanceStageNoHitShots++;
	}
	m_DynamicBalanceShotActive = false;
}

void Game::EvaluateDynamicBalanceStage(bool cleared)
{
	if (!m_DynamicBalanceStageActive)
	{
		return;
	}

	FinishDynamicBalanceShot();

	const int safeEnemyCount =
		(std::max)(1, m_DynamicBalanceStageEnemyCount);
	const float targetShots =
		m_DynamicBalanceTargetShotsPerEnemy *
		static_cast<float>(safeEnemyCount);
	m_DynamicBalanceLastHpRatio =
		m_PlayerRunStatus.maxHp <= 0
			? 0.0f
			: std::clamp(
				static_cast<float>(m_PlayerRunStatus.currentHp) /
					static_cast<float>(m_PlayerRunStatus.maxHp),
				0.0f,
				1.0f);
	m_DynamicBalanceLastNoHitRate =
		m_DynamicBalanceStageShots <= 0
			? 0.0f
			: static_cast<float>(
				m_DynamicBalanceStageNoHitShots) /
				static_cast<float>(m_DynamicBalanceStageShots);
	m_DynamicBalanceLastShotsPerEnemy =
		static_cast<float>(m_DynamicBalanceStageShots) /
		static_cast<float>(safeEnemyCount);
	m_DynamicBalanceLastResult =
		cleared ? "clear" : "game_over";
	m_DynamicBalanceLastLevelChange = 0;

	int requestedChange = 0;
	if (!m_DynamicBalanceEnabled)
	{
		m_DynamicBalanceLastReason =
			"Automatic adjustment is disabled.";
	}
	else if (!cleared)
	{
		requestedChange = -1;
		m_DynamicBalanceLastReason =
			"Game over: reduce the next battle difficulty.";
	}
	else
	{
		const bool strongClear =
			m_DynamicBalanceLastHpRatio >=
				m_DynamicBalanceStrongHpRatio &&
			m_DynamicBalanceLastNoHitRate <=
				m_DynamicBalanceStrongNoHitRate &&
			static_cast<float>(m_DynamicBalanceStageShots) <=
				targetShots;
		const bool weakClear =
			m_DynamicBalanceLastHpRatio <=
				m_DynamicBalanceWeakHpRatio ||
			m_DynamicBalanceLastNoHitRate >=
				m_DynamicBalanceWeakNoHitRate ||
			static_cast<float>(m_DynamicBalanceStageShots) >
				targetShots *
				m_DynamicBalanceWeakShotMultiplier;

		if (strongClear)
		{
			requestedChange = 1;
			m_DynamicBalanceLastReason =
				"Strong clear: raise the next battle difficulty.";
		}
		else if (weakClear)
		{
			requestedChange = -1;
			m_DynamicBalanceLastReason =
				"Struggling clear: reduce the next battle difficulty.";
		}
		else
		{
			m_DynamicBalanceLastReason =
				"Performance is inside the target range.";
		}
	}

	const int previousLevel = m_DynamicBalanceLevel;
	m_DynamicBalanceLevel = std::clamp(
		m_DynamicBalanceLevel + requestedChange,
		m_DynamicBalanceMinLevel,
		m_DynamicBalanceMaxLevel);
	m_DynamicBalanceLastLevelChange =
		m_DynamicBalanceLevel - previousLevel;
	m_DynamicBalanceStageActive = false;

	std::cout
		<< "[DynamicBalance] Result="
		<< m_DynamicBalanceLastResult
		<< " HP=" << m_DynamicBalanceLastHpRatio
		<< " NoHit=" << m_DynamicBalanceLastNoHitRate
		<< " ShotsPerEnemy="
		<< m_DynamicBalanceLastShotsPerEnemy
		<< " Level=" << previousLevel
		<< "->" << m_DynamicBalanceLevel
		<< " Reason=" << m_DynamicBalanceLastReason
		<< std::endl;
}

bool Game::UpdateBalanceAutoPlay()
{
	if (Input::GetKeyTrigger(VK_F8))
	{
		m_BalanceAutoPlayEnabled =
			!m_BalanceAutoPlayEnabled;
		m_AutoDecisionFrame = 0;

		std::cout
			<< "[BalanceAutoPlay] "
			<< (m_BalanceAutoPlayEnabled ? "ON" : "OFF")
			<< std::endl;
	}

	if (!m_BalanceAutoPlayEnabled || m_Scene == nullptr)
	{
		return false;
	}

	auto isDecisionReady = [this]()
	{
		m_AutoDecisionFrame++;
		if (m_AutoDecisionFrame < m_AutoDecisionDelayFrames)
		{
			return false;
		}

		m_AutoDecisionFrame = 0;
		return true;
	};

	if (dynamic_cast<TitleScene*>(m_Scene) != nullptr)
	{
		if (!isDecisionReady())
		{
			return false;
		}

		if (m_AutoMaxRuns > 0 &&
			m_AutoRunCount >= m_AutoMaxRuns)
		{
			m_BalanceAutoPlayEnabled = false;
			std::cout
				<< "[BalanceAutoPlay] Max runs reached"
				<< std::endl;
			return false;
		}

		StartNewRun();
		m_AutoRunCount++;
		ChangeScene(SceneType::Select);
		return true;
	}

	if (dynamic_cast<ResultScene*>(m_Scene) != nullptr)
	{
		if (!m_AutoRestartAfterGameOver)
		{
			m_BalanceAutoPlayEnabled = false;
			return false;
		}

		if (!isDecisionReady())
		{
			return false;
		}

		ChangeScene(SceneType::Title);
		m_GameState = GameState::AimingDirection;
		return true;
	}

	if (dynamic_cast<StageSelectScene*>(m_Scene) != nullptr)
	{
		if (!isDecisionReady())
		{
			return false;
		}

		PruneBalanceAutoPendingBalls();
		if (IsBalanceAutoHealNeeded())
		{
			std::cout
				<< "[BalanceAutoPlay] Route=RestSite Reason=HP below 40%"
				<< std::endl;
			ChangeScene(SceneType::RestSite);
			return true;
		}

		if (FindBalanceAutoPendingUpgradeableBall() >= 0)
		{
			std::cout
				<< "[BalanceAutoPlay] Route=RestSite Reason=Ball upgrade pending"
				<< std::endl;
			ChangeScene(SceneType::RestSite);
			return true;
		}

		if (FindBalanceAutoPendingRemovalBall() >= 0 &&
			m_PlayerRunStatus.money >= kAutoShopRemoveCost &&
			m_PlayerDeck.GetRewardTargetCount() > 1)
		{
			std::cout
				<< "[BalanceAutoPlay] Route=Shop Reason=Ball removal pending"
				<< std::endl;
			ChangeScene(SceneType::Shop);
			return true;
		}

		StartNextBattle(GetBalanceAutoStageType());
		return true;
	}

	if (dynamic_cast<RestSiteScene*>(m_Scene) != nullptr)
	{
		if (!isDecisionReady())
		{
			return false;
		}

		if (IsBalanceAutoHealNeeded() && RestHeal())
		{
			std::cout
				<< "[BalanceAutoPlay] RestSite: recovered "
				<< GetRestHealPercent()
				<< "% of max HP"
				<< std::endl;
		}
		else
		{
			const int ballIndex =
				FindBalanceAutoPendingUpgradeableBall();
			const PlayerBallData* ball =
				m_PlayerDeck.GetRewardTarget(ballIndex);
			if (ball != nullptr)
			{
				const std::uint64_t instanceId = ball->instanceId;
				const std::string definitionId = ball->definitionId;
				if (RestUpgradeBall(ballIndex))
				{
					std::cout
						<< "[BalanceAutoPlay] RestSite: upgraded "
						<< definitionId
						<< " (instance " << instanceId << ")"
						<< std::endl;
				}
			}
		}

		StartNextBattle(GetBalanceAutoStageType());
		return true;
	}

	if (dynamic_cast<ShopScene*>(m_Scene) != nullptr)
	{
		if (!isDecisionReady())
		{
			return false;
		}

		const int ballIndex =
			FindBalanceAutoPendingRemovalBall();
		const PlayerBallData* ball =
			m_PlayerDeck.GetRewardTarget(ballIndex);
		if (ball != nullptr)
		{
			const std::uint64_t instanceId = ball->instanceId;
			const std::string definitionId = ball->definitionId;
			if (RemoveShopBall(
				ballIndex,
				kAutoShopRemoveCost))
			{
				std::cout
					<< "[BalanceAutoPlay] Shop: removed "
					<< definitionId
					<< " (instance " << instanceId << ")"
					<< std::endl;
			}
		}

		StartNextBattle(GetBalanceAutoStageType());
		return true;
	}

	if (m_GameState == GameState::ClearReward)
	{
		if (!isDecisionReady())
		{
			return false;
		}

		if (!m_IsClearRewardChosen)
		{
			ApplyBalanceAutoReward();
		}
		else
		{
			ChangeScene(SceneType::Select);
			m_GameState = GameState::AimingDirection;
		}
		return true;
	}

	if (dynamic_cast<BattleScene*>(m_Scene) != nullptr &&
		m_GameState == GameState::AimingDirection)
	{
		std::vector<PlayerBall*> players =
			GetComponents<PlayerBall>();
		if (players.empty() ||
			players[0] == nullptr ||
			!players[0]->IsIdle() ||
			!AreAllBallsStopped())
		{
			m_AutoDecisionFrame = 0;
			return false;
		}

		if (!isDecisionReady())
		{
			return false;
		}

		SelectBalanceAutoBall();
		return FireBalanceAutoShot();
	}

	return false;
}

bool Game::ContainsComponent(const Component* component) const
{
	return component != nullptr &&
		ContainsGameObject(component->GetGameObject());
}

void Game::SelectBalanceAutoBall()
{
	const int offerCount = m_PlayerDeck.GetOfferCount();
	if (offerCount <= 0)
	{
		return;
	}

	int bestIndex = 0;
	float bestScore =
		-(std::numeric_limits<float>::max)();
	const bool needsDefense =
		m_PlayerRunStatus.currentHp * 2 <=
		m_PlayerRunStatus.maxHp;

	for (int index = 0; index < offerCount; index++)
	{
		const PlayerBallData* ball =
			m_PlayerDeck.GetOffer(index);
		if (ball == nullptr)
		{
			continue;
		}

		float score =
			static_cast<float>(ball->status.attack) * 4.0f +
			static_cast<float>(ball->status.defense) *
				(needsDefense ? 3.0f : 1.0f);
		score += (std::max)(0.0f, ball->status.mass - 2.0f);
		if (ball->status.abilities.pierce)
		{
			score += 2.0f;
		}

		if (score > bestScore)
		{
			bestScore = score;
			bestIndex = index;
		}
	}

	m_SelectedOfferIndex = bestIndex;
	if (m_SelectedHoldIndex == bestIndex)
	{
		m_SelectedHoldIndex = -1;
	}
	ApplySelectedBallPreview();
}

bool Game::FireBalanceAutoShot()
{
	std::vector<PlayerBall*> players =
		GetComponents<PlayerBall>();
	std::vector<EnemyBall*> enemies =
		GetComponents<EnemyBall>();

	if (players.empty() || players[0] == nullptr)
	{
		return false;
	}

	PlayerBall* player = players[0];
	const DirectX::SimpleMath::Vector3 playerPosition =
		player->GetPosition();

	EnemyBall* bestTarget = nullptr;
	DirectX::SimpleMath::Vector3 bestDirection =
		DirectX::SimpleMath::Vector3::UnitZ;
	float bestDistance = 0.0f;
	float bestTargetScore =
		-(std::numeric_limits<float>::max)();

	for (EnemyBall* target : enemies)
	{
		if (target == nullptr || target->IsDefeated() ||
			target->IsPocketed())
		{
			continue;
		}

		DirectX::SimpleMath::Vector3 direction =
			target->GetPosition() - playerPosition;
		direction.y = 0.0f;
		const float distance = direction.Length();
		if (distance <= 0.0001f)
		{
			continue;
		}
		direction /= distance;

		float chainScore = 0.0f;
		for (EnemyBall* other : enemies)
		{
			if (other == nullptr ||
				other == target ||
				other->IsDefeated() ||
				other->IsPocketed())
			{
				continue;
			}

			DirectX::SimpleMath::Vector3 relative =
				other->GetPosition() - target->GetPosition();
			relative.y = 0.0f;
			const float forward =
				relative.Dot(direction);
			if (forward <= 0.0f)
			{
				continue;
			}

			const float lateralSquared =
				(std::max)(
					0.0f,
					relative.LengthSquared() -
					forward * forward);
			const float targetRadius =
				target->GetBall() == nullptr
				? 0.0f
				: target->GetBall()->GetRadius();
			const float otherRadius =
				other->GetBall() == nullptr
				? 0.0f
				: other->GetBall()->GetRadius();
			const float chainWidth =
				targetRadius + otherRadius + 1.5f;

			if (lateralSquared <= chainWidth * chainWidth)
			{
				chainScore +=
					100.0f / (1.0f + forward * 0.05f);
			}
		}

		const float targetScore =
			chainScore -
			distance * 0.02f +
			static_cast<float>(target->GetAttack()) * 0.25f;
		if (targetScore > bestTargetScore)
		{
			bestTargetScore = targetScore;
			bestTarget = target;
			bestDirection = direction;
			bestDistance = distance;
		}
	}

	if (bestTarget == nullptr)
	{
		return false;
	}

	constexpr float DegreesToRadians =
		3.14159265358979323846f / 180.0f;
	std::uniform_real_distribution<float> jitterDistribution(
		-m_AutoAimJitterDegrees,
		m_AutoAimJitterDegrees);
	const float jitter =
		jitterDistribution(m_AutoRandomEngine) *
		DegreesToRadians;
	const float cosine = std::cos(jitter);
	const float sine = std::sin(jitter);
	const DirectX::SimpleMath::Vector3 shotDirection(
		bestDirection.x * cosine +
			bestDirection.z * sine,
		0.0f,
		-bestDirection.x * sine +
			bestDirection.z * cosine);

	const float shotPower = std::clamp(
		3.0f + bestDistance * 0.03f,
		m_AutoMinShotPower,
		m_AutoMaxShotPower);

	std::cout
		<< "[BalanceAutoPlay] Target="
		<< bestTarget->GetEnemyId()
		<< " Power=" << shotPower
		<< std::endl;

	player->FireAutomatedShot(
		shotDirection * shotPower);
	return true;
}

void Game::ApplyBalanceAutoReward()
{
	// 削除待ちのボールがある場合は、ショップ費用を優先して確保する。
	if (FindBalanceAutoPendingRemovalBall() >= 0 &&
		m_PlayerRunStatus.money < kAutoShopRemoveCost)
	{
		const int moneyBefore = m_PlayerRunStatus.money;
		m_SelectedRewardIndex = 2;
		m_PlayerRunStatus.money += kExtraRewardMoney;
		m_RewardMessage =
			"Auto Play: saved Money for a pending ball removal.";
		m_IsClearRewardChosen = true;
		RecordBalanceEvent(
			"clear_reward_choice",
			{
				{ "controller", "autoplay" },
				{ "reward", "extra_money" },
				{ "reason", "save_for_ball_removal" },
				{ "money_before", moneyBefore },
				{ "money_after", m_PlayerRunStatus.money },
			});
		return;
	}

	// 通常時は強化報酬を選ばない。強化は衝突条件を満たした個体だけ、
	// 休憩所で実行する。
	for (int index = 0;
		index < m_PlayerDeck.GetCatalogCount();
		index++)
	{
		if (m_PlayerDeck.AddCatalogBall(index))
		{
			m_SelectedRewardIndex = 0;
			m_SelectedRewardBallIndex = index;
			m_RewardMessage =
				"Auto Play: added a new ball.";
			m_IsClearRewardChosen = true;
			RecordBalanceEvent(
				"clear_reward_choice",
				{
					{ "controller", "autoplay" },
					{ "reward", "new_ball" },
					{ "catalog_index", index },
				});
			return;
		}
	}

	const int moneyBefore = m_PlayerRunStatus.money;
	m_SelectedRewardIndex = 2;
	m_PlayerRunStatus.money += kExtraRewardMoney;
	m_RewardMessage =
		"Auto Play: received extra Money.";
	m_IsClearRewardChosen = true;
	RecordBalanceEvent(
		"clear_reward_choice",
		{
			{ "controller", "autoplay" },
			{ "reward", "extra_money" },
			{ "money_before", moneyBefore },
			{ "money_after", m_PlayerRunStatus.money },
		});
}

StageType Game::GetBalanceAutoStageType() const
{
	return GetScheduledStageType();
}

bool Game::IsBalanceAutoHealNeeded() const
{
	return m_PlayerRunStatus.maxHp > 0 &&
		m_PlayerRunStatus.currentHp * 10 <
		m_PlayerRunStatus.maxHp * 4;
}

int Game::FindBalanceAutoPendingUpgradeableBall() const
{
	for (const std::uint64_t instanceId :
		m_AutoPendingBallAdjustments)
	{
		for (int index = 0;
			index < m_PlayerDeck.GetRewardTargetCount();
			index++)
		{
			const PlayerBallData* ball =
				m_PlayerDeck.GetRewardTarget(index);
			if (ball != nullptr &&
				ball->instanceId == instanceId &&
				ball->CanUpgrade())
			{
				return index;
			}
		}
	}

	return -1;
}

int Game::FindBalanceAutoPendingRemovalBall() const
{
	for (const std::uint64_t instanceId :
		m_AutoPendingBallAdjustments)
	{
		for (int index = 0;
			index < m_PlayerDeck.GetRewardTargetCount();
			index++)
		{
			const PlayerBallData* ball =
				m_PlayerDeck.GetRewardTarget(index);
			if (ball != nullptr &&
				ball->instanceId == instanceId &&
				!ball->CanUpgrade())
			{
				return index;
			}
		}
	}

	return -1;
}

bool Game::IsBallAdjustmentCandidate(
	std::uint64_t instanceId) const
{
	return instanceId != 0 &&
		std::find(
			m_AutoPendingBallAdjustments.begin(),
			m_AutoPendingBallAdjustments.end(),
			instanceId) !=
		m_AutoPendingBallAdjustments.end();
}

bool Game::HasAvailableRestBenefit() const
{
	if (CanRestHeal())
	{
		return true;
	}

	const int ballCount =
		m_PlayerDeck.GetRewardTargetCount();
	for (int index = 0; index < ballCount; index++)
	{
		const PlayerBallData* ball =
			m_PlayerDeck.GetRewardTarget(index);
		if (ball != nullptr && ball->CanUpgrade())
		{
			return true;
		}
	}

	return false;
}

void Game::RemoveBalanceAutoPendingBall(
	std::uint64_t instanceId)
{
	m_AutoPendingBallAdjustments.erase(
		std::remove(
			m_AutoPendingBallAdjustments.begin(),
			m_AutoPendingBallAdjustments.end(),
			instanceId),
		m_AutoPendingBallAdjustments.end());
}

void Game::PruneBalanceAutoPendingBalls()
{
	m_AutoPendingBallAdjustments.erase(
		std::remove_if(
			m_AutoPendingBallAdjustments.begin(),
			m_AutoPendingBallAdjustments.end(),
			[this](std::uint64_t instanceId)
			{
				for (int index = 0;
					index < m_PlayerDeck.GetRewardTargetCount();
					index++)
				{
					const PlayerBallData* ball =
						m_PlayerDeck.GetRewardTarget(index);
					if (ball != nullptr &&
						ball->instanceId == instanceId)
					{
						return false;
					}
				}
				return true;
			}),
		m_AutoPendingBallAdjustments.end());
}

void Game::OnBattleStageStarted(const StageData& stage)
{
	m_CurrentBattleStageType = stage.stageType;
	m_PocketedEnemyQueue.clear();
	m_DynamicBalanceAppliedEnabled = m_DynamicBalanceEnabled;
	m_DynamicBalanceAppliedLevel = m_DynamicBalanceLevel;
	m_DynamicBalanceStageActive = true;
	m_DynamicBalanceShotActive = false;
	m_DynamicBalanceCurrentShotHit = false;
	m_DynamicBalanceStageShots = 0;
	m_DynamicBalanceStageNoHitShots = 0;
	m_DynamicBalanceStageEnemyCount =
		static_cast<int>(stage.enemies.size());

	std::vector<BalanceEnemySnapshot> enemies;
	enemies.reserve(stage.enemies.size());

	for (const EnemySpawnData& spawn : stage.enemies)
	{
		const BallStatus& status = spawn.enemyData.status;
		BalanceEnemySnapshot snapshot;
		snapshot.id = spawn.enemyData.id;
		snapshot.maxHp = status.maxHp;
		snapshot.attack = status.attack;
		snapshot.defense = status.defense;
		snapshot.mass = status.mass;
		snapshot.radius = status.radius;
		snapshot.restitution = status.restitution;
		snapshot.friction = status.friction;
		snapshot.positionX = spawn.position.x;
		snapshot.positionY = spawn.position.y;
		snapshot.positionZ = spawn.position.z;
		enemies.push_back(std::move(snapshot));
	}

	nlohmann::json deckJson = nlohmann::json::array();
	for (int index = 0;
		index < m_PlayerDeck.GetRewardTargetCount();
		index++)
	{
		const PlayerBallData* ball =
			m_PlayerDeck.GetRewardTarget(index);
		if (ball == nullptr)
		{
			continue;
		}
		deckJson.push_back(
			{
				{ "instance_id", ball->instanceId },
				{ "id", ball->definitionId },
				{ "upgrade_level", ball->upgradeLevel },
				{ "attack", GetEffectivePlayerBallAttack(ball) },
				{ "defense", GetEffectivePlayerBallDefense(ball) },
			});
	}

	nlohmann::json relicsJson = nlohmann::json::array();
	for (const RelicDefinition& relic : RelicCatalog)
	{
		if (HasRelic(relic.type))
		{
			relicsJson.push_back(relic.name);
		}
	}

	const int appliedHpModifier =
		m_DynamicBalanceAppliedEnabled
		? m_DynamicBalanceAppliedLevel * m_DynamicBalanceHpStep
		: 0;
	const int appliedAttackModifier =
		m_DynamicBalanceAppliedEnabled
		? CalculateDynamicBalanceAttackModifier(
			m_DynamicBalanceAppliedLevel)
		: 0;
	const int progressionAttackModifier =
		CalculateProgressionAttackModifier();
	const std::string layoutSource =
		m_McpCurrentStageOverride.has_value()
		? "mcp_override"
		: (stage.enemies.size() >= 4
			? "dense_auto_layout"
			: "stage_data");
	float baseThreatBudget = 0.0f;
	for (const EnemySpawnData& spawn : stage.enemies)
	{
		const auto cost = m_EnemyThreatCosts.find(spawn.enemyId);
		baseThreatBudget += cost != m_EnemyThreatCosts.end()
			? cost->second
			: 10.0f;
	}
	const float layoutThreatMultiplier =
		layoutSource == "dense_auto_layout"
		? m_DenseLayoutThreatMultiplier
		: (layoutSource == "mcp_override"
			? m_McpLayoutThreatMultiplier
			: m_StageDataLayoutThreatMultiplier);
	const float actualThreatBudget =
		std::round(baseThreatBudget * layoutThreatMultiplier * 100.0f) /
		100.0f;
	const auto targetThreat = m_StageThreatTargets.find(stage.id);
	const nlohmann::json stageContext =
	{
		{ "progress", m_PlayerRunStatus.progress },
		{ "par", stage.par },
		{ "money", m_PlayerRunStatus.money },
		{ "layout_source", layoutSource },
		{ "deck", std::move(deckJson) },
		{ "owned_relics", std::move(relicsJson) },
		{
			"baseline_difficulty",
			{
				{ "profile", m_BaselineDifficultyProfile },
				{ "enemy_hp_multiplier", m_BaselineEnemyHpMultiplier },
				{ "enemy_attack_delta", m_BaselineEnemyAttackDelta },
			}
		},
		{
			"encounter_threat",
			{
				{ "base_budget", baseThreatBudget },
				{ "layout_multiplier", layoutThreatMultiplier },
				{ "actual_budget", actualThreatBudget },
				{
					"target_budget",
					targetThreat != m_StageThreatTargets.end()
						? nlohmann::json(targetThreat->second)
						: nlohmann::json(nullptr)
				},
				{
					"deviation_from_target",
					targetThreat != m_StageThreatTargets.end()
						? nlohmann::json(
							actualThreatBudget - targetThreat->second)
						: nlohmann::json(nullptr)
				},
			}
		},
		{
			"dynamic_balance",
			{
				{ "enabled", m_DynamicBalanceAppliedEnabled },
				{ "applied_level", m_DynamicBalanceAppliedLevel },
				{ "enemy_hp_modifier", appliedHpModifier },
				{ "enemy_attack_modifier", appliedAttackModifier },
			}
		},
		{
			"progression_scaling",
			{
				{ "enabled", m_ProgressionScalingEnabled },
				{ "progress", m_PlayerRunStatus.progress },
				{ "enemy_attack_modifier", progressionAttackModifier },
				{ "attack_start_progress", m_ProgressionAttackStart },
				{ "attack_interval", m_ProgressionAttackInterval },
				{ "maximum_attack_delta",
					m_ProgressionAttackMaximumDelta },
			}
		},
		{
			"assist_mode",
			{
				{ "enabled", m_DynamicBalanceAppliedEnabled },
				{ "applied_level", m_DynamicBalanceAppliedLevel },
			}
		},
	};

	BalanceLogger::GetInstance().BeginStage(
		stage.id,
		ToString(stage.stageType),
		stage.difficulty,
		m_PlayerRunStatus.currentHp,
		m_PlayerRunStatus.maxHp,
		enemies,
		stageContext);
}

bool Game::RestHeal()
{
	RestSiteScene* restSite =
		dynamic_cast<RestSiteScene*>(m_Scene);
	if (restSite != nullptr && restSite->HasUsedAction())
	{
		return false;
	}
	if (!CanRestHeal())
	{
		return false;
	}

	const int hpBefore = m_PlayerRunStatus.currentHp;
	const int configuredHealAmount = GetRestHealAmount();
	m_PlayerRunStatus.currentHp = (std::min)(
		m_PlayerRunStatus.maxHp,
		hpBefore + configuredHealAmount);
	const int actualHealAmount =
		m_PlayerRunStatus.currentHp - hpBefore;
	if (restSite != nullptr)
	{
		restSite->MarkActionUsed();
	}
	m_RestHealCooldownRemaining = m_RestHealCooldownBattles;
	RecordBalanceEvent(
		"rest_heal",
		{
			{ "hp_before", hpBefore },
			{ "hp_after", m_PlayerRunStatus.currentHp },
			{ "heal_amount", actualHealAmount },
			{ "configured_heal_amount", configuredHealAmount },
			{ "heal_ratio", m_RestHealRatio },
			{ "cooldown_battles", m_RestHealCooldownBattles },
			{ "cooldown_remaining", m_RestHealCooldownRemaining },
			{ "capped_at_max_hp",
				actualHealAmount < configuredHealAmount },
			{ "source_scene", GetSceneDebugName(m_Scene) },
		});
	return true;
}

int Game::GetRestHealAmount() const
{
	return (std::max)(
		1,
		static_cast<int>(std::ceil(
			static_cast<float>(m_PlayerRunStatus.maxHp) *
			m_RestHealRatio)));
}

int Game::GetRestHealPercent() const
{
	return static_cast<int>(std::lround(m_RestHealRatio * 100.0f));
}

bool Game::RestUpgradeBall(int ballIndex)
{
	RestSiteScene* restSite =
		dynamic_cast<RestSiteScene*>(m_Scene);
	if (restSite != nullptr && restSite->HasUsedAction())
	{
		return false;
	}
	PlayerBallData* ball = m_PlayerDeck.GetRewardTarget(ballIndex);
	if (ball == nullptr ||
		!ball->CanUpgrade())
	{
		return false;
	}

	const std::uint64_t instanceId = ball->instanceId;
	const std::string ballId = ball->definitionId;
	const int upgradeLevelBefore = ball->upgradeLevel;
	const BallUpgradeStep& upgrade = ball->upgradeTable[ball->upgradeLevel];
	ball->status.attack = upgrade.attack;
	ball->status.defense = upgrade.defense;
	ball->upgradeLevel++;
	ball->status = NormalizeBallStatus(ball->status);
	RemoveBalanceAutoPendingBall(instanceId);
	if (restSite != nullptr)
	{
		restSite->MarkActionUsed();
	}
	RecordBalanceEvent(
		"ball_upgraded",
		{
			{ "instance_id", instanceId },
			{ "ball_id", ballId },
			{ "upgrade_level_before", upgradeLevelBefore },
			{ "upgrade_level_after", ball->upgradeLevel },
			{ "attack_after", ball->status.attack },
			{ "defense_after", ball->status.defense },
			{ "source_scene", GetSceneDebugName(m_Scene) },
		});
	return true;
}

bool Game::BuyShopBall(int catalogIndex, int cost)
{
	cost = (std::max)(0, cost);
	const PlayerBallData* catalogBall =
		m_PlayerDeck.GetCatalogBall(catalogIndex);
	const std::string ballId =
		catalogBall != nullptr ? catalogBall->definitionId : std::string();
	const int moneyBefore = m_PlayerRunStatus.money;
	if (m_PlayerRunStatus.money < cost || !m_PlayerDeck.AddCatalogBall(catalogIndex))
	{
		return false;
	}

	m_PlayerRunStatus.money -= cost;
	RecordBalanceEvent(
		"shop_ball_purchased",
		{
			{ "catalog_index", catalogIndex },
			{ "ball_id", ballId },
			{ "cost", cost },
			{ "money_before", moneyBefore },
			{ "money_after", m_PlayerRunStatus.money },
		});
	return true;
}

bool Game::RemoveShopBall(int ballIndex, int cost)
{
	cost = (std::max)(0, cost);
	if (m_PlayerRunStatus.money < cost ||
		m_PlayerDeck.GetRewardTargetCount() <=
			PlayerDeck::MinimumDeckSize)
	{
		return false;
	}

	const PlayerBallData* ball =
		m_PlayerDeck.GetRewardTarget(ballIndex);
	if (ball == nullptr)
	{
		return false;
	}
	const std::uint64_t instanceId = ball->instanceId;
	const std::string ballId = ball->definitionId;
	const int moneyBefore = m_PlayerRunStatus.money;

	if (!m_PlayerDeck.RemoveRewardTarget(ballIndex))
	{
		return false;
	}

	m_PlayerRunStatus.money -= cost;
	RemoveBalanceAutoPendingBall(instanceId);
	RecordBalanceEvent(
		"shop_ball_removed",
		{
			{ "instance_id", instanceId },
			{ "ball_id", ballId },
			{ "cost", cost },
			{ "money_before", moneyBefore },
			{ "money_after", m_PlayerRunStatus.money },
			{ "deck_count_after", m_PlayerDeck.GetRewardTargetCount() },
		});
	return true;
}

bool Game::BuyRelic(int relicIndex)
{
	const RelicDefinition* relic = GetRelic(relicIndex);
	if (relic == nullptr || HasRelic(relic->type))
	{
		return false;
	}

	const int cost = (std::max)(0, relic->price);
	if (m_PlayerRunStatus.money < cost)
	{
		return false;
	}

	const int moneyBefore = m_PlayerRunStatus.money;
	m_PlayerRunStatus.money -= cost;
	m_OwnedRelics[static_cast<std::size_t>(relic->type)] = true;

	const std::vector<PlayerBall*> players = GetComponents<PlayerBall>();
	for (PlayerBall* player : players)
	{
		ApplyRelicModifiersTo(player);
	}
	RecordBalanceEvent(
		"relic_purchased",
		{
			{ "relic_index", relicIndex },
			{ "relic_name", relic->name },
			{ "cost", cost },
			{ "money_before", moneyBefore },
			{ "money_after", m_PlayerRunStatus.money },
		});

	return true;
}

int Game::GetOwnedRelicCount() const
{
	return static_cast<int>(std::count(
		m_OwnedRelics.begin(),
		m_OwnedRelics.end(),
		true));
}

int Game::GetRelicAttackBonus() const
{
	return HasRelic(RelicType::AllBallAttackUp) ? 1 : 0;
}

int Game::GetRelicDefenseBonus() const
{
	return HasRelic(RelicType::AllBallDefenseUp) ? 1 : 0;
}

int Game::GetEffectivePlayerBallAttack(
	const PlayerBallData* ball) const
{
	return ball == nullptr
		? 0
		: ball->status.attack + GetRelicAttackBonus();
}

int Game::GetEffectivePlayerBallDefense(
	const PlayerBallData* ball) const
{
	return ball == nullptr
		? 0
		: ball->status.defense + GetRelicDefenseBonus();
}

void Game::ApplyPlayerStatusTo(PlayerBall* player)
{
	if (player == nullptr)
	{
		return;
	}

	const PlayerBallData* selectedBall = m_PlayerDeck.GetCurrent();
	if (selectedBall == nullptr)
	{
		selectedBall = m_PlayerDeck.GetOffer(m_SelectedOfferIndex);
	}

	if (selectedBall == nullptr)
	{
		ApplyPlayerRunStatusTo(player);
		return;
	}

	player->SetStatus(selectedBall->status);

	// 物理半径だけでなく、描画モデルの大きさも選択したボールへ合わせる。
	if (selectedBall->status.radius > 0.0f && player->GetBall() != nullptr)
	{
		const float visualScale = selectedBall->status.radius;
		player->GetBall()->SetScale(DirectX::SimpleMath::Vector3(
			visualScale,
			visualScale,
			visualScale));
	}

	ApplyPlayerRunStatusTo(player);
}

void Game::ApplyPlayerRunStatusTo(PlayerBall* player)
{
	if (player == nullptr)
	{
		return;
	}

	m_PlayerRunStatus = NormalizePlayerRunStatus(m_PlayerRunStatus);
	player->SetMaxHP(m_PlayerRunStatus.maxHp);
	player->SetHP(m_PlayerRunStatus.currentHp);
	ApplyRelicModifiersTo(player);
}

void Game::ApplyRelicModifiersTo(PlayerBall* player)
{
	if (player == nullptr || player->GetBall() == nullptr)
	{
		return;
	}

	const int collisionBonus =
		HasRelic(RelicType::CollisionAttackUp)
			? m_CurrentShotCollisionAttackBonus
			: 0;
	player->GetBall()->SetCombatModifiers(
		GetRelicAttackBonus() + collisionBonus,
		GetRelicDefenseBonus());
}

void Game::ResetShotRelicState(PlayerBall* player)
{
	m_CurrentShotCollisionAttackBonus = 0;
	m_CurrentShotPlayerEnemyCollisionCount = 0;
	m_CurrentShotEnemyEnemyCollisionCount = 0;
	m_CurrentShotBankShotReady = false;
	m_CurrentShotBankShotConsumed = false;
	if (player != nullptr)
	{
		ApplyRelicModifiersTo(player);
	}
}

void Game::ApplyEndOfShotRelicEffects(PlayerBall* player)
{
	if (player == nullptr ||
		!HasRelic(RelicType::EmergencyRepairKit) ||
		GetCurrentShotBallCollisionCount() <
			kEmergencyRepairContactThreshold)
	{
		return;
	}

	const int hpBefore = player->GetHP();
	const int hpAfter = (std::min)(
		player->GetMaxHP(),
		hpBefore + kEmergencyRepairHealAmount);
	if (hpAfter <= hpBefore)
	{
		return;
	}

	player->SetHP(hpAfter);
	CapturePlayerStatusFrom(player);
	RecordBalanceEvent(
		"emergency_repair_triggered",
		{
			{ "ball_collision_count", GetCurrentShotBallCollisionCount() },
			{ "heal_amount", hpAfter - hpBefore },
			{ "hp_before", hpBefore },
			{ "hp_after", hpAfter },
		});
}

void Game::CapturePlayerStatusFrom(const PlayerBall* player)
{
	if (player == nullptr)
	{
		return;
	}

	BallStatus updatedStatus =
		NormalizeBallStatus(player->GetStatus());

	PlayerBallData* currentBall = m_PlayerDeck.GetCurrent();
	if (currentBall != nullptr)
	{
		const int ballMaxHp = currentBall->status.maxHp;
		currentBall->status = updatedStatus;
		currentBall->status.maxHp = ballMaxHp;
	}

	m_PlayerRunStatus = NormalizePlayerRunStatus(m_PlayerRunStatus);
	m_PlayerRunStatus.currentHp = std::clamp(
		player->GetHP(),
		0,
		m_PlayerRunStatus.maxHp
	);
}
void Game::CaptureCurrentPlayerStatus()
{
	std::vector<PlayerBall*> players = GetComponents<PlayerBall>();
	if (players.empty())
	{
		return;
	}
	CapturePlayerStatusFrom(players[0]);
}

void Game::DrawNextPlayerBall()
{
	m_PlayerDeck.DrawNext();
}
void Game::PrepareNextPlayerBall()
{
	DiscardCurrentPlayerBall();

	if (m_PlayerDeck.HasCurrent())
	{
		return;
	}

	BeginBallSelection();
}

int Game::CalculateStageRewardMoney() const
{
	constexpr int BASE_CLEAR_MONEY = 5;
	int totalRewardMoney = BASE_CLEAR_MONEY;

	std::vector<EnemyBall*> enemies =
		m_Instance->GetComponents<EnemyBall>();

	for (EnemyBall* enemy : enemies)
	{
		if (enemy == nullptr)
		{
			continue;
		}

		// 倒した敵の報酬だけを取得する
		if (!enemy->IsDefeated())
		{
			continue;
		}

		totalRewardMoney +=
			(std::max)(0, enemy->GetRewardMoney());
	}

	return totalRewardMoney;
}

void Game::CollectStageRewardMoney()
{
	// StartClearRewardが複数回呼ばれても二重取得しない
	if (m_IsStageRewardCollected)
	{
		return;
	}

	m_CurrentStageRewardMoney =
		CalculateStageRewardMoney();

	const int moneyBefore = m_PlayerRunStatus.money;
	m_PlayerRunStatus.money +=
		m_CurrentStageRewardMoney;

	m_PlayerRunStatus =
		NormalizePlayerRunStatus(m_PlayerRunStatus);

	m_IsStageRewardCollected = true;

	m_RewardMessage =
		"Stage Reward: +" +
		std::to_string(m_CurrentStageRewardMoney) +
		" Money";
	RecordBalanceEvent(
		"stage_money_reward",
		{
			{ "amount", m_CurrentStageRewardMoney },
			{ "money_before", moneyBefore },
			{ "money_after", m_PlayerRunStatus.money },
		});
}

void Game::OnPlayerShotFired(PlayerBall* player)
{
	ResetShotRelicState(player);
	if (player != nullptr && player->GetBall() != nullptr)
	{
		player->GetBall()->ResetShotAbilityState();
	}

	// 選択内容はショットした瞬間に確定する。
	if (!m_PlayerDeck.HasCurrent())
	{
		if (!m_PlayerDeck.SelectOffer(
			m_SelectedOfferIndex,
			m_SelectedHoldIndex))
		{
			return;
		}
	}

	if (player != nullptr)
	{
		CapturePlayerStatusFrom(player);
	}

	const PlayerBallData* currentBall =
		m_PlayerDeck.GetCurrent();
	if (player != nullptr && currentBall != nullptr)
	{
		if (m_DynamicBalanceStageActive)
		{
			FinishDynamicBalanceShot();
			m_DynamicBalanceStageShots++;
			m_DynamicBalanceShotActive = true;
			m_DynamicBalanceCurrentShotHit = false;
		}

		const DirectX::SimpleMath::Vector3 position =
			player->GetPosition();
		const DirectX::SimpleMath::Vector3 velocity =
			player->GetVelocity();
		const std::vector<EnemyBall*> enemies =
			GetComponents<EnemyBall>();
		nlohmann::json offers = nlohmann::json::array();
		for (int index = 0;
			index < m_PlayerDeck.GetOfferCount();
			index++)
		{
			const PlayerBallData* offer =
				m_PlayerDeck.GetOffer(index);
			if (offer == nullptr)
			{
				continue;
			}
			offers.push_back(
				{
					{ "offer_index", index },
					{ "instance_id", offer->instanceId },
					{ "id", offer->definitionId },
					{ "upgrade_level", offer->upgradeLevel },
					{ "held", m_PlayerDeck.WasHeldOffer(index) },
				});
		}
		nlohmann::json shotContext =
		{
			{ "selected_offer_index", m_SelectedOfferIndex },
			{ "held_offer_index", m_SelectedHoldIndex },
			{ "selected_instance_id", currentBall->instanceId },
			{ "effective_attack", player->GetAttack() },
			{ "effective_defense", player->GetDefense() },
			{ "offers", std::move(offers) },
			{ "mcp_telemetry", m_PendingShotTelemetry },
		};

		BalanceLogger::GetInstance().BeginShot(
			currentBall->definitionId,
			currentBall->upgradeLevel,
			velocity.Length(),
			position.x,
			position.y,
			position.z,
			velocity.x,
			velocity.y,
			velocity.z,
			player->GetHP(),
			CountAliveEnemies(enemies),
			CountDefeatedEnemies(enemies),
			shotContext);
		m_PendingShotTelemetry = nlohmann::json::object();
	}

	m_PlayerDeck.MarkCurrentUsed();
}

void Game::NotifyPlayerWallCollision()
{
	if (!HasRelic(RelicType::BankShot) ||
		m_CurrentShotBankShotConsumed)
	{
		return;
	}

	m_CurrentShotBankShotReady = true;
}

int Game::ConsumeBankShotDamageMultiplier()
{
	if (!HasRelic(RelicType::BankShot) ||
		!m_CurrentShotBankShotReady ||
		m_CurrentShotBankShotConsumed)
	{
		return 1;
	}

	m_CurrentShotBankShotReady = false;
	m_CurrentShotBankShotConsumed = true;
	RecordBalanceEvent(
		"bank_shot_triggered",
		{
			{ "damage_multiplier", kBankShotDamageMultiplier },
		});
	return kBankShotDamageMultiplier;
}

void Game::NotifyDamageBallCollision(
	DamageBallCollisionType collisionType)
{
	if (collisionType == DamageBallCollisionType::PlayerEnemy)
	{
		m_CurrentShotPlayerEnemyCollisionCount++;
	}
	else
	{
		m_CurrentShotEnemyEnemyCollisionCount++;
	}

	if (!HasRelic(RelicType::CollisionAttackUp))
	{
		return;
	}

	m_CurrentShotCollisionAttackBonus++;
	const std::vector<PlayerBall*> players = GetComponents<PlayerBall>();
	for (PlayerBall* player : players)
	{
		ApplyRelicModifiersTo(player);
	}
}

void Game::NotifyPlayerDamage(
	const std::string& source,
	int damage,
	const std::string& sourceId)
{
	BalanceLogger::GetInstance().RecordPlayerDamage(
		source,
		damage,
		sourceId);
}

void Game::RecordBalanceEvent(
	const std::string& eventType,
	const nlohmann::json& details)
{
	BalanceLogger::GetInstance().RecordEvent(eventType, details);
}

void Game::NotifyDynamicBalanceHit()
{
	if (m_DynamicBalanceStageActive &&
		m_DynamicBalanceShotActive)
	{
		m_DynamicBalanceCurrentShotHit = true;
	}
}

void Game::ApplyDynamicBalanceToEnemyData(
	EnemyData& enemyData) const
{
	enemyData.status.maxHp = std::clamp(
		static_cast<int>(std::lround(
			static_cast<double>(enemyData.status.maxHp) *
			static_cast<double>(m_BaselineEnemyHpMultiplier))),
		m_DynamicBalanceMinEnemyHp,
		m_DynamicBalanceMaxEnemyHp);
	enemyData.status.attack = std::clamp(
		enemyData.status.attack +
			m_BaselineEnemyAttackDelta +
			CalculateProgressionAttackModifier(),
		m_DynamicBalanceMinEnemyAttack,
		m_DynamicBalanceMaxEnemyAttack);

	const bool effectiveEnabled =
		m_DynamicBalanceStageActive
			? m_DynamicBalanceAppliedEnabled
			: m_DynamicBalanceEnabled;
	if (!effectiveEnabled)
	{
		return;
	}

	const int effectiveLevel =
		m_DynamicBalanceStageActive
			? m_DynamicBalanceAppliedLevel
			: m_DynamicBalanceLevel;
	const int hpDelta =
		effectiveLevel *
		m_DynamicBalanceHpStep;
	const int attackDelta =
		CalculateDynamicBalanceAttackModifier(effectiveLevel);

	enemyData.status.maxHp = std::clamp(
		enemyData.status.maxHp + hpDelta,
		m_DynamicBalanceMinEnemyHp,
		m_DynamicBalanceMaxEnemyHp);
	enemyData.status.attack = std::clamp(
		enemyData.status.attack + attackDelta,
		m_DynamicBalanceMinEnemyAttack,
		m_DynamicBalanceMaxEnemyAttack);
}

int Game::CalculateProgressionAttackModifier() const
{
	if (!m_ProgressionScalingEnabled ||
		m_PlayerRunStatus.progress < m_ProgressionAttackStart ||
		m_ProgressionAttackStep <= 0)
	{
		return 0;
	}
	const int tier = 1 +
		(m_PlayerRunStatus.progress - m_ProgressionAttackStart) /
		m_ProgressionAttackInterval;
	return (std::min)(
		m_ProgressionAttackMaximumDelta,
		tier * m_ProgressionAttackStep);
}

int Game::CalculateDynamicBalanceAttackModifier(int level) const
{
	if (level > 0 && !m_DynamicBalancePositiveAttackScalingEnabled)
	{
		return 0;
	}
	return (level / m_DynamicBalanceLevelsPerAttackStep) *
		m_DynamicBalanceAttackStep;
}

void Game::SetDynamicBalance(
	bool enabled,
	bool resetLevel,
	int requestedLevel,
	bool hasRequestedLevel)
{
	if (m_BalanceValidationEnabled &&
		m_BalanceValidationCurrentDisableDynamicBalance)
	{
		m_DynamicBalanceEnabled = false;
		m_DynamicBalanceLevel = 0;
		m_DynamicBalanceLastReason =
			"Assist mode is locked off by balance validation mode.";
		m_DynamicBalanceLastLevelChange = 0;
		return;
	}
	m_DynamicBalanceEnabled = enabled;
	if (hasRequestedLevel)
	{
		m_DynamicBalanceLevel = std::clamp(
			requestedLevel,
			m_DynamicBalanceMinLevel,
			m_DynamicBalanceMaxLevel);
		m_DynamicBalanceLastReason =
			"Difficulty level was set through MCP.";
	}
	else if (resetLevel)
	{
		m_DynamicBalanceLevel = 0;
		m_DynamicBalanceLastReason =
			"Difficulty level was reset through MCP.";
	}
	else
	{
		m_DynamicBalanceLastReason =
			enabled
				? "Automatic adjustment was enabled through MCP."
				: "Automatic adjustment was disabled through MCP.";
	}
	m_DynamicBalanceLastLevelChange = 0;
}

void Game::NotifyBalanceAutoFullHpEnemySurvived()
{
	const PlayerBallData* currentBall =
		m_PlayerDeck.GetCurrent();
	if (currentBall == nullptr ||
		currentBall->instanceId == 0)
	{
		return;
	}

	if (std::find(
		m_AutoPendingBallAdjustments.begin(),
		m_AutoPendingBallAdjustments.end(),
		currentBall->instanceId) !=
		m_AutoPendingBallAdjustments.end())
	{
		return;
	}

	m_AutoPendingBallAdjustments.push_back(
		currentBall->instanceId);
	std::cout
		<< "[BalanceAutoPlay] Ball adjustment pending: "
		<< currentBall->definitionId
		<< " (instance " << currentBall->instanceId << ")"
		<< std::endl;
}

void Game::DiscardCurrentPlayerBall()
{
	if (!m_PlayerDeck.HasCurrent())
	{
		m_PlayerDeck.ClearCurrentUsed();
		return;
	}

	if (!m_PlayerDeck.IsCurrentUsed())
	{
		return;
	}

	std::vector<PlayerBall*> players = GetComponents<PlayerBall>();
	if (!players.empty() && players[0] != nullptr)
	{
		CapturePlayerStatusFrom(players[0]);
	}

	m_PlayerDeck.DiscardCurrentIfUsed();
}

void Game::SaveDebugSnapshot()
{
	std::ofstream file("debug_state_snapshot.txt");

	if (!file)
	{
		return;
	}

	file << std::fixed << std::setprecision(3);

	file << "[Scene]\n";
	file << "Scene = " << GetSceneDebugName(m_Scene) << "\n";
	file << "GameState = " << GetGameStateDebugName(m_GameState)
		<< " (" << static_cast<int>(m_GameState) << ")\n";
	file << "AreAllBallsStopped = " << (AreAllBallsStopped() ? "true" : "false") << "\n";
	file << "AreAllEnemiesDefeated = " << (AreAllEnemiesDefeated() ? "true" : "false") << "\n";
	file << "\n";

	std::vector<PlayerBall*> players = GetComponents<PlayerBall>();
	std::vector<EnemyBall*> enemies = GetComponents<EnemyBall>();

	file << "[BallCounts]\n";
	file << "PlayerRunCurrentHp = "
		<< m_PlayerRunStatus.currentHp << "\n";

	file << "PlayerRunMaxHp = "
		<< m_PlayerRunStatus.maxHp << "\n";

	file << "[Deck]\n";
	file << "DrawPile = "
		<< m_PlayerDeck.GetDrawPileCount() << "\n";

	file << "DiscardPile = "
		<< m_PlayerDeck.GetDiscardPileCount() << "\n";

	file << "OfferCount = "
		<< m_PlayerDeck.GetOfferCount() << "\n";

	file << "TotalDeckCount = "
		<< m_PlayerDeck.GetRewardTargetCount() << "\n";

	const PlayerBallData* currentBall = m_PlayerDeck.GetCurrent();
	if (currentBall != nullptr)
	{
		file << "CurrentBallId = "
			<< currentBall->definitionId
			<< "\n";

		file << "CurrentBallAttack = "
			<< currentBall->status.attack
			<< "\n";

		file << "CurrentBallUsed = "
			<< (m_PlayerDeck.IsCurrentUsed() ? "true" : "false")
			<< "\n";
	}
	else
	{
		file << "CurrentBallId = none\n";
	}

	file << "\n";
	file << "PlayerBall = " << players.size() << "\n";
	file << "EnemyBall = " << enemies.size() << "\n";
	file << "\n";

	file << "PlayerMoney = "
		<< m_PlayerRunStatus.money
		<< "\n";

	for (int i = 0; i < static_cast<int>(players.size()); i++)
	{
		WriteBallDebugStatus(file, "PlayerBall", i, players[i]->GetBall());
	}

	for (int i = 0; i < static_cast<int>(enemies.size()); i++)
	{
		WriteBallDebugStatus(file, "EnemyBall", i, enemies[i]->GetBall());
	}
}
