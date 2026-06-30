#include "Game.h"
#include "Renderer.h"
#include "input.h"

#include "PlayerBall.h"  // DrawImGui呼び出しに必要
#include "EnemyBall.h"   // DrawImGui呼び出しに必要
#include "BallBase.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h" 

Game* Game::m_Instance;//ゲームインスタンス

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



	// ▼ Update() 内の shrink_to_fit() の直後に追加 ▼

	// ゲーム状態の更新（全ボール停止検出）
	if (m_Instance->m_GameState == GameState::BallsMoving)
	{
		std::vector<BallBase*> balls = m_Instance->GetObjects<BallBase>();
		if (!balls.empty())                     // TC-08: 0体の場合は遷移しない
		{
			bool allStopped = true;
			for (BallBase* ball : balls)
			{
				if (!ball->IsStopped())         // TC-07: 1体でも動いていれば維持
				{
					allStopped = false;
					break;
				}
			}
			if (allStopped)                     // TC-06: 全停止で TurnEnd へ
			{
				m_Instance->m_GameState = GameState::TurnEnd;
			}
		}
	}
	else if (m_Instance->m_GameState == GameState::TurnEnd) // TC-09: 翌フレームで自動遷移
	{
		m_Instance->m_GameState = GameState::AimingDirection;
	}
}

// 描画
void Game::Draw()
{
	SkyBox* sky = Game::GetInstance()->GetSkyBox();
	if (sky)
		sky->Draw(&m_Instance->m_Camera);

	Renderer::DrawStart();

	for (auto& o : m_Instance->m_Objects)
		o->Draw(&m_Instance->m_Camera);

	// ★ ImGuiのBegin〜EndをDrawEnd()の前に移動
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

	// ★ Render と RenderDrawData も DrawEnd()の前に移動
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	Renderer::DrawEnd(); // ← Present は最後
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
