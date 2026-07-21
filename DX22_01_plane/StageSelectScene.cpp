#include "StageSelectScene.h"
#include "Game.h"
#include "Input.h"
#include "Texture2D.h"

// コンストラクタ
StageSelectScene::StageSelectScene()
{
	Init();
}

// デストラクタ
StageSelectScene::~StageSelectScene()
{
	Uninit();
}

// 初期化
void StageSelectScene::Init()
{

	SelectArrow = 2; //enumにてステージ1の値が2に設定されているため

	//背景画像オブジェクトを作成
	Texture2D* pt = Game::GetInstance()->AddObject<Texture2D>();
	pt->SetTexture("assets/texture/background1.png");
	pt->SetPosition(0.0f, 0.0f, 0.0f);
	pt->SetRotation(0.0f, 0.0f, 0.0f);
	pt->SetScale(1280.0f, 720.0f, 0.0f);
	m_MySceneObjects.emplace_back(pt);

	
	m_pArrowImage = Game::GetInstance()->AddObject<Texture2D>();
	m_pArrowImage->SetTexture("assets/texture/golf_jou_man.png");
	m_pArrowImage->SetPosition(0.0f, -SelectArrow * 100.0f, 0.0f);
	m_pArrowImage->SetRotation(0.0f, 0.0f, 0.0f);
	m_pArrowImage->SetScale(150.0f, 150.0f, 0.0f);
	m_MySceneObjects.emplace_back(m_pArrowImage);


}

// 更新
void StageSelectScene::Update()
{
	bool isChanged = false;//矢印が動いたかどうか

	if (Input::GetKeyTrigger(VK_S))
	{
		if (SelectArrow < 4) SelectArrow += 1;
		else SelectArrow = 2;
		isChanged = true;
	}
	if (Input::GetKeyTrigger(VK_W))
	{
		if (SelectArrow > 2) SelectArrow -= 1;
		else SelectArrow = 4;
		isChanged = true;
	}

	// 値が変わっていたら、画像の位置を更新する
	if (isChanged && m_pArrowImage != nullptr)
	{
		// Initと同じ計算式で座標を再設定
		m_pArrowImage->SetPosition(0.0f, -SelectArrow * 100.0f, 0.0f);
	}

	// エンターキーを押してステージ1へ
	if (Input::GetKeyTrigger(VK_RETURN))
	{
		switch (SelectArrow)
		{
		case 2:
			Game::GetInstance()->ChangeScene(STAGE1);
			break;
		case 3:
			Game::GetInstance()->ChangeScene(STAGE2);
			break;
		case 4:
			Game::GetInstance()->ChangeScene(STAGE3);
			break;
		default:
			break;
		}

	}
}

// 終了処理
void StageSelectScene::Uninit()
{
	// このシーンのオブジェクトを削除する
	for (auto& o : m_MySceneObjects) {
		Game::GetInstance()->DeleteComponent(o);
	}
	m_MySceneObjects.clear();
}
