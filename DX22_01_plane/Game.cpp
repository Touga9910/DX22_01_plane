#include "Game.h"
#include "Renderer.h"
#include "input.h"

#include "PlayerBall.h"  // DrawImGui呼び出しに必要
#include "EnemyBall.h"   // DrawImGui呼び出しに必要
#include "BallBase.h"
#include "PlayerBallDataLoader.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <random>

Game* Game::m_Instance;//ゲームインスタンス

namespace
{
	enum class RewardTargetType
	{
		SingleBall,
		WholeDeck,
		PlayerOverall,
	};

	struct RewardDefinition
	{
		const char* name;
		RewardTargetType targetType;
	};

	constexpr RewardDefinition kRewardDefinitions[] =
	{
		{ "Attack Up", RewardTargetType::SingleBall },
		{ "Defense Up", RewardTargetType::SingleBall },
		{ "Ball Max HP Up", RewardTargetType::SingleBall },
	};

	constexpr int kRewardCount =
		static_cast<int>(sizeof(kRewardDefinitions) / sizeof(kRewardDefinitions[0]));

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
		if (dynamic_cast<Stage1Scene*>(scene)) return "STAGE1";
		if (dynamic_cast<Stage2Scene*>(scene)) return "STAGE2";
		if (dynamic_cast<Stage3Scene*>(scene)) return "STAGE3";
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

	void WriteBallDebugStatus(std::ofstream& file, const char* typeName, int index, BallBase* ball)
	{
		if (ball == nullptr)
		{
			return;
		}

		const Transform transform = ball->GetTransform();

		file << "[" << typeName << " " << index << "]\n";
		WriteVector3(file, "Position", transform.position);
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
	DeleteAllObject();
}

// 初期化
void Game::Init()
{
	// 静的インスタンスをここで1つだけ生成
	if (m_Instance == nullptr) {
		m_Instance = new Game();
	}
	// 描画終了処理
	Renderer::Init();

	//入力処理初期化
	Input::Create();

	// カメラ初期化
	m_Instance->m_Camera.Init();

	m_Instance->LoadPlayerStatusFromJson();

	//最初のシーンを読みこむ
	m_Instance->m_Scene = new TitleScene;
}

// 更新
void Game::Update()
{
	// 入力処理更新
	Input::Update();

	// ==========================
	// ClearReward中はゲーム本編を更新しない
	// ==========================
	if (m_Instance->m_GameState == GameState::ClearReward)
	{
		m_Instance->UpdateClearReward();
		return;
	}

	//シーン更新
	m_Instance->m_Scene->Update();

	// カメラ更新
	m_Instance->m_Camera.Update();

	//オブジェクト更新
	for (auto& o : m_Instance->m_Objects)
	{
		o->Update();
	}
	// 死亡フラグ（HPが0など）が立っているオブジェクトを自動・動的削除
	std::erase_if(m_Instance->m_Objects, [](const std::unique_ptr<Object>& o) {
		if (o && o->IsDead()) {
			o->Uninit(); // 削除される前に終了処理を呼ぶ
			return true; // 配列から削除する
		}
		return false;    // 残す
		});

	// 要素が減った場合はメモリを詰める（既存の処理をここに集約）
	m_Instance->m_Objects.shrink_to_fit();

	// ゲーム状態の更新（全ボール停止検出）
	switch (m_Instance->m_GameState)
	{
	case GameState::BallsMoving:
		if (m_Instance->AreAllBallsStopped())
		{
			if (m_Instance->AreAllEnemiesDefeated())
			{
				m_Instance->StartClearReward();

				// 攻撃はしないのでリターン
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
		m_Instance->m_GameState = GameState::AimingDirection;
		break;

	case GameState::GameOver:
		m_Instance->ProcessGameOver();
		break;

	default:
		break;
	}
}

// 描画
/*
void Game::Draw()
{
	SkyBox* sky = Game::GetInstance()->GetSkyBox();
	if (sky)
		sky->Draw(&m_Instance->m_Camera);

	Renderer::DrawStart();

	for (auto& o : m_Instance->m_Objects)
		o->Draw(&m_Instance->m_Camera);

	// ★ ImGuiのBegin?EndをDrawEnd()の前に移動
	ImGui::Begin("Ball Debugger");

	std::vector<PlayerBall*> players = m_Instance->GetObjects<PlayerBall>();
	for (int i = 0; i < players.size(); i++)
		players[i]->DrawImGui();

	std::vector<EnemyBall*> enemies = m_Instance->GetObjects<EnemyBall>();
	for (int i = 0; i < enemies.size(); i++)
	{
		std::string label = "EnemyBall " + std::to_string(i);
		enemies[i]->DrawImGui(label);
	}

	ImGui::End();

	// ==========================
	// 報酬UIを最後に重ねる
	// ==========================
	if (m_Instance->m_GameState == GameState::ClearReward)
	{
		m_Instance->DrawClearRewardUI();
	}

	// Render と RenderDrawData も DrawEnd()の前に移動
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	Renderer::DrawEnd(); // ← Present は最後
}
*/

void Game::Draw()
{
	SkyBox* sky = Game::GetInstance()->GetSkyBox();
	if (sky)
	{
		sky->Draw(&m_Instance->m_Camera);
	}

	Renderer::DrawStart();

	for (auto& o : m_Instance->m_Objects)
	{
		o->Draw(&m_Instance->m_Camera);
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

	if (ImGui::Button("Save Debug Snapshot"))
	{
		m_Instance->SaveDebugSnapshot();
	}
	ImGui::SameLine();
	ImGui::TextUnformatted("debug_state_snapshot.txt");

	std::vector<PlayerBall*> players = m_Instance->GetObjects<PlayerBall>();
	for (int i = 0; i < players.size(); i++)
	{
		players[i]->DrawImGui();
	}

	std::vector<EnemyBall*> enemies = m_Instance->GetObjects<EnemyBall>();
	for (int i = 0; i < enemies.size(); i++)
	{
		std::string label = "EnemyBall " + std::to_string(i);
		enemies[i]->DrawImGui(label);
	}

	ImGui::End();

	// ==========================
	// 報酬UIを最後に重ねる
	// ==========================
	if (m_Instance->m_GameState == GameState::ClearReward)
	{
		m_Instance->DrawClearRewardUI();
	}

	// ==========================
	// ImGui描画確定
	// ==========================
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	Renderer::DrawEnd();
}

// 終了処理
void Game::Uninit()
{
	// カメラ終了処理
	m_Instance->m_Camera.Uninit();

	//オブジェクト終了処理
	for (auto& o : m_Instance->m_Objects)
	{
		o->Uninit();;
	}


	//入力処理終了
	Input::Release();

	// 描画終了処理
	Renderer::Uninit();

	//インスタンス削除
	delete m_Instance;
}

//インスタンス取得
Game* Game::GetInstance()
{
	return m_Instance;
}

//シーン切り替え
void Game::ChangeScene(SceneName sName)
{
	//読み込みシーンあれば削除
	int score = 0;
	if (m_Instance->m_Scene != nullptr)
	{
		m_Instance->CaptureCurrentPlayerStatus();

		// 消そうとするシーンがStage1ならスコアを保存する
		if (Stage1Scene* sObj = dynamic_cast<Stage1Scene*>(m_Instance->m_Scene))
		{
			score = sObj->GetScore();
		}
		delete m_Instance->m_Scene;
		m_Instance->m_Scene = nullptr;
	}

	switch (sName)
	{
	case TITLE:
		m_Instance->m_Scene = new TitleScene;	//メモリ確保
		break;
	case STAGE1:
		m_Instance->m_Scene = new Stage1Scene;	//メモリ確保
		break;
	case STAGE2:
		m_Instance->m_Scene = new Stage2Scene;	//メモリ確保
		break;
	case STAGE3:
		m_Instance->m_Scene = new Stage3Scene;	//メモリ確保
		break;
	case SELECT:
		m_Instance->m_Scene = new StageSelectScene;	//メモリ確保
		break;
	case RESULT:
		m_Instance->m_Scene = new ResultScene;	//メモリ確保
		dynamic_cast<ResultScene*>(m_Instance->m_Scene)->SetScore(score);//スコアを設定
		break;
	default:
		break;
	}
}

//オブジェクトを削除
void Game::DeleteObject(Object* pt)
{
	if (pt == nullptr) return;

	// 1. まず、本当に m_Objects の中に pt が存在するか確認する
	auto it = std::find_if(m_Instance->m_Objects.begin(), m_Instance->m_Objects.end(),
		[pt](const std::unique_ptr<Object>& element) {
			return element.get() == pt;
		});

	// 2. 存在しない（すでに消えている）なら何もしない
	if (it == m_Instance->m_Objects.end()) return;

	// 3. 存在する場合のみ、安全に終了して削除
	pt->Uninit();

	m_Instance->m_Objects.erase(it);

	// ※終了時のループ中に shrink_to_fit() を高頻度で呼ぶとメモリ再確保で落ちやすいため、
	// 削除処理の直後ではなく、ゲーム全体のUpdateの最後などで呼ぶのが安全です。
}

//オブジェクトを全削除
void Game::DeleteAllObject()
{
	//終了処理
	for (auto& o : m_Instance->m_Objects)
	{
		o->Uninit();
	}
	m_Instance->m_Objects.clear();//全て削除
	m_Instance->m_Objects.shrink_to_fit();
}

SkyBox* Game::GetSkyBox()
{
	{
		if (m_Instance)
		{
			return m_Instance->m_SkyBox;
		}
		return nullptr;
	}
}

bool Game::ContainsObject(const Object* pt) const
{
	if (pt == nullptr) return false;

	for (const auto& o : m_Objects)
	{
		if (o.get() == pt)
		{
			return true;
		}
	}

	return false;
}

bool Game::AreAllEnemiesDefeated() const
{
	std::vector<EnemyBall*> enemies = m_Instance->GetObjects<EnemyBall>();

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
	std::vector<BallBase*> balls = m_Instance->GetObjects<BallBase>();

	if (balls.empty())
	{
		return false;
	}

	// Game::AreAllBallsStopped()
	for (BallBase* ball : balls)
	{
		if (ball == nullptr) continue;

		// 撃破済みボールは停止判定から除外
		if (ball->IsDefeated()) continue;

		if (!ball->IsStopped())
		{
			return false;
		}
	}

	return true;
}

void Game::ProcessEnemyAttack()
{
	std::vector<PlayerBall*> players = GetObjects<PlayerBall>();
	std::vector<EnemyBall*> enemies = GetObjects<EnemyBall>();

	if (players.empty())
	{
		m_GameState = GameState::GameOver;
		return;
	}

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

		enemy->Attack(player);
	}

	CapturePlayerStatusFrom(player);

	if (m_PlayerRunStatus.currentHp <= 0)
	{
		m_GameState = GameState::GameOver;
	}
	else
	{
		m_GameState = GameState::TurnEnd;
	}
}

void Game::ProcessGameOver()
{
	DiscardCurrentPlayerBall();
	ChangeScene(RESULT);
	m_GameState = GameState::AimingDirection;
}

void Game::StartClearReward()
{
	DiscardCurrentPlayerBall();
	m_SelectedRewardIndex = 0;
	m_SelectedRewardBallIndex = 0;
	m_GameState = GameState::ClearReward;
}

void Game::UpdateClearReward()
{
	const int rewardCount = kRewardCount;
	const int ballCount = m_PlayerDeck.GetRewardTargetCount();

	if (Input::GetKeyTrigger(VK_UP))
	{
		m_SelectedRewardIndex--;

		if (m_SelectedRewardIndex < 0)
		{
			m_SelectedRewardIndex = rewardCount - 1;
		}
	}

	if (Input::GetKeyTrigger(VK_DOWN))
	{
		m_SelectedRewardIndex++;

		if (m_SelectedRewardIndex >= rewardCount)
		{
			m_SelectedRewardIndex = 0;
		}
	}

	if (ballCount > 0)
	{
		if (Input::GetKeyTrigger(VK_LEFT))
		{
			m_SelectedRewardBallIndex--;

			if (m_SelectedRewardBallIndex < 0)
			{
				m_SelectedRewardBallIndex = ballCount - 1;
			}
		}

		if (Input::GetKeyTrigger(VK_RIGHT))
		{
			m_SelectedRewardBallIndex++;

			if (m_SelectedRewardBallIndex >= ballCount)
			{
				m_SelectedRewardBallIndex = 0;
			}
		}
	}
	else
	{
		m_SelectedRewardBallIndex = 0;
	}

	if (Input::GetKeyTrigger(VK_SPACE))
	{
		ApplyReward(m_SelectedRewardIndex);

		// 仮：報酬選択後にステージ選択へ戻る
		// 次ステージ制にするなら、ここを STAGE2 / STAGE3 などに変更
		ChangeScene(SELECT);

		m_GameState = GameState::AimingDirection;
	}
}

void Game::DrawClearRewardUI()
{
	ImGui::SetNextWindowPos(ImVec2(300.0f, 120.0f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(560.0f, 360.0f), ImGuiCond_Always);

	ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoCollapse;

	ImGui::Begin("Clear Reward UI", nullptr, flags);

	ImGui::Text("CLEAR!");
	ImGui::Separator();

	ImGui::Text("Reward Select");
	for (int i = 0; i < kRewardCount; i++)
	{
		if (i == m_SelectedRewardIndex)
		{
			ImGui::Text(" > [ %s ]", kRewardDefinitions[i].name);
		}
		else
		{
			ImGui::Text("   %s", kRewardDefinitions[i].name);
		}
	}

	ImGui::Separator();
	ImGui::Text("Target Ball");

	const int ballCount = m_PlayerDeck.GetRewardTargetCount();
	for (int i = 0; i < ballCount; i++)
	{
		const PlayerBallData* ball = m_PlayerDeck.GetRewardTarget(i);
		if (ball == nullptr)
		{
			continue;
		}

		const char* mark = (i == m_SelectedRewardBallIndex) ? " >" : "  ";
		ImGui::Text(
			"%s [%d] %s  ATK:%d DEF:%d BallHP:%d",
			mark,
			i,
			ball->definitionId.c_str(),
			ball->status.attack,
			ball->status.defense,
			ball->status.maxHp
		);
	}

	if (ballCount <= 0)
	{
		ImGui::Text("No target ball");
	}

	ImGui::Separator();
	ImGui::Text("UP / DOWN : Reward");
	ImGui::Text("LEFT / RIGHT : Target Ball");
	ImGui::Text("SPACE : Decide");

	ImGui::End();
}

void Game::ApplyReward(int rewardIndex)
{
	if (rewardIndex < 0 || rewardIndex >= kRewardCount)
	{
		return;
	}

	const RewardDefinition& reward = kRewardDefinitions[rewardIndex];

	switch (reward.targetType)
	{
	case RewardTargetType::SingleBall:
	{
		PlayerBallData* targetBall =
			m_PlayerDeck.GetRewardTarget(m_SelectedRewardBallIndex);
		if (targetBall == nullptr)
		{
			return;
		}

		switch (rewardIndex)
		{
		case 0:
			targetBall->status.attack += 1;
			break;

		case 1:
			targetBall->status.defense += 1;
			break;

		case 2:
			targetBall->status.maxHp += 1;
			break;

		default:
			break;
		}

		PlayerBallData* currentBall = m_PlayerDeck.GetCurrent();
		if (targetBall == currentBall)
		{
			std::vector<PlayerBall*> players = GetObjects<PlayerBall>();
			if (!players.empty())
			{
				ApplyPlayerStatusTo(players[0]);
			}
		}
		break;
	}

	case RewardTargetType::WholeDeck:
	case RewardTargetType::PlayerOverall:
	default:
		break;
	}
}
void Game::LoadPlayerStatusFromJson(const std::string& filePath)
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
	m_PlayerDeck.SetDefaultDeck(loadResult.defaultDeck);

	ResetPlayerRuntimeStatus();
}
void Game::ResetPlayerRuntimeStatus()
{
	m_PlayerRunStatus = NormalizePlayerRunStatus(m_DefaultPlayerRunStatus);
	m_PlayerDeck.Reset();
}
void Game::ApplyPlayerStatusTo(PlayerBall* player)
{
	if (player == nullptr)
	{
		return;
	}

	if (!m_PlayerDeck.HasCurrent())
	{
		m_PlayerDeck.DrawNext();
	}

	const PlayerBallData* currentBall = m_PlayerDeck.GetCurrent();
	if (currentBall == nullptr)
	{
		return;
	}

	player->SetStatus(currentBall->status);
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
	std::vector<PlayerBall*> players = GetObjects<PlayerBall>();
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

	DrawNextPlayerBall();

	std::vector<PlayerBall*> players =
		GetObjects<PlayerBall>();

	if (!players.empty())
	{
		ApplyPlayerStatusTo(players[0]);
	}
}

void Game::OnPlayerShotFired(PlayerBall* player)
{
	if (player != nullptr)
	{
		CapturePlayerStatusFrom(player);
	}

	if (!m_PlayerDeck.HasCurrent())
	{
		return;
	}

	m_PlayerDeck.MarkCurrentUsed();
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

	std::vector<PlayerBall*> players = GetObjects<PlayerBall>();
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

	std::vector<PlayerBall*> players = GetObjects<PlayerBall>();
	std::vector<EnemyBall*> enemies = GetObjects<EnemyBall>();

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

	for (int i = 0; i < static_cast<int>(players.size()); i++)
	{
		WriteBallDebugStatus(file, "PlayerBall", i, players[i]);
	}

	for (int i = 0; i < static_cast<int>(enemies.size()); i++)
	{
		WriteBallDebugStatus(file, "EnemyBall", i, enemies[i]);
	}
}
