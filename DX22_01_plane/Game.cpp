#include "Game.h"
#include "Renderer.h"
#include "input.h"

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
	//インスタンス作成
	m_Instance = new Game;
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
}

// 描画
void Game::Draw()
{
	//sスカイボックス描画
	SkyBox* sky = Game::GetInstance()->GetSkyBox();
	if (sky)
	{
		sky->Draw(&m_Instance->m_Camera);
	}

	// 描画前処理
	Renderer::DrawStart();

	//オブジェクト初期化
	for (auto& o : m_Instance->m_Objects)
	{

		o->Draw(&m_Instance->m_Camera);
	}

	// 描画後処理
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
	if (pt == NULL)return;

	pt->Uninit();

	//要素削除
	erase_if(m_Instance->m_Objects,
		[pt](const std::unique_ptr<Object>& element) {
			return element.get() == pt;
		});
		m_Instance->m_Objects.shrink_to_fit();
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
