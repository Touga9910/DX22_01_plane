
#include "TitleScene.h"
#include "Game.h"
#include "Input.h"
#include "Texture2D.h"

// コンストラクタ
TitleScene::TitleScene()
{
	Init();
}

// デストラクタ
TitleScene::~TitleScene()
{
	Uninit();
}

// 初期化
void TitleScene::Init()
{
	//背景画像オブジェクトを作成
	Texture2D* pt = Game::GetInstance()->AddObject<Texture2D>();
	pt->SetTexture("assets/texture/background1.png");
	pt->SetPosition(0.0f, 0.0f, 0.0f);
	pt->SetRotation(0.0f, 0.0f, 0.0f);
	pt->SetScale(1280.0f, 720.0f, 0.0f);
	m_MySceneObjects.emplace_back(pt);

	Texture2D* pt2 = Game::GetInstance()->AddObject<Texture2D>();
	pt2->SetTexture("assets/texture/titlerogo.png");
	pt2->SetPosition(0.0f, 100.0f, 0.0f);
	pt2->SetRotation(0.0f, 0.0f, 0.0f);
	pt2->SetScale(700.0f, 150.0f, 0.0f);
	m_MySceneObjects.emplace_back(pt2);
}

// 更新
void TitleScene::Update()
{

	if (Input::GetKeyTrigger(VK_W))
	{
		m_CursolPos++;
		if (m_CursolPos > 1)
		{
			m_CursolPos = 0;
		}
		std::cout << m_CursolPos << std::endl;
	}
	if (Input::GetKeyTrigger(VK_S))
	{
		m_CursolPos--;
		if (m_CursolPos < 0)
		{
			m_CursolPos = 1;
		}
		std::cout << m_CursolPos << std::endl;
	}

	// エンターキーを押してステージセレクトへ
	if (Input::GetKeyTrigger(VK_RETURN))
	{
		switch (m_CursolPos)
		{
		case 0:
			Game::GetInstance()->StartNewRun();
			Game::GetInstance()->ChangeScene(SceneType::Select);
			break;
		case 1:

			break;
		default:
			break;
		}
	}
}

// 終了処理
void TitleScene::Uninit()
{
	// このシーンのオブジェクトを削除する
	for (auto& o : m_MySceneObjects) {
		Game::GetInstance()->DeleteComponent(o);
	}
	m_MySceneObjects.clear();
}
