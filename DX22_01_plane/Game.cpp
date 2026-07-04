#include "Game.h"
#include "Renderer.h"
#include "input.h"

#include "PlayerBall.h"  // DrawImGui呼び出しに必要
#include "EnemyBall.h"   // DrawImGui呼び出しに必要
#include "BallBase.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"

#include <fstream>
#include <iomanip>

Game* Game::m_Instance;//ゲームインスタンス

namespace
{
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

	if (player->GetHP() <= 0)
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
	ChangeScene(RESULT);
	m_GameState = GameState::AimingDirection;
}

void Game::StartClearReward()
{
	m_SelectedRewardIndex = 0;
	m_GameState = GameState::ClearReward;
}

void Game::UpdateClearReward()
{
	const int rewardCount = 3;

	if (Input::GetKeyTrigger(VK_LEFT))
	{
		m_SelectedRewardIndex--;

		if (m_SelectedRewardIndex < 0)
		{
			m_SelectedRewardIndex = rewardCount - 1;
		}
	}

	if (Input::GetKeyTrigger(VK_RIGHT))
	{
		m_SelectedRewardIndex++;

		if (m_SelectedRewardIndex >= rewardCount)
		{
			m_SelectedRewardIndex = 0;
		}
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
	ImGui::SetNextWindowSize(ImVec2(500.0f, 300.0f), ImGuiCond_Always);

	ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoCollapse;

	ImGui::Begin("Clear Reward UI", nullptr, flags);

	ImGui::Text("CLEAR!");
	ImGui::Separator();

	ImGui::Text("Reward Select");

	const char* rewards[3] =
	{
		"Heal",
		"Max HP Up",
		"Power Up"
	};

	for (int i = 0; i < 3; i++)
	{
		if (i == m_SelectedRewardIndex)
		{
			ImGui::Text(" > [ %s ]", rewards[i]);
		}
		else
		{
			ImGui::Text("   %s", rewards[i]);
		}
	}

	ImGui::Separator();
	ImGui::Text("LEFT / RIGHT : Select");
	ImGui::Text("SPACE : Decide");

	ImGui::End();
}

void Game::ApplyReward(int rewardIndex)
{
	std::vector<PlayerBall*> players = GetObjects<PlayerBall>();

	if (players.empty())
	{
		return;
	}

	PlayerBall* player = players[0];

	switch (rewardIndex)
	{
	case 0:
		// Heal
		player->SetHP(player->GetMaxHP());
		break;

	case 1:
		// Max HP Up
		player->SetMaxHP(player->GetMaxHP() + 1);
		player->SetHP(player->GetMaxHP());
		break;

	case 2:
		// Power Up
		// 現在 PlayerBall 側に攻撃力・ショット強化用関数がないため仮実装
		// 後で player->AddShotPower(1.0f); のような関数を追加する
		break;

	default:
		break;
	}
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