#include "ResultScene.h"
#include "Game.h"
#include "Input.h"
#include "Texture2D.h"
#include "Texture2DFactory.h"

// コンストラクタ
ResultScene::ResultScene()
{
	Init();
}

// デストラクタ
ResultScene::~ResultScene()
{
	Uninit();
}

// 初期化
void ResultScene::Init()
{
	//背景画像オブジェクトを作成
	Texture2D* pt = Texture2DFactory::Create(*Game::GetInstance());
	pt->SetTexture("assets/texture/background2.png");
	pt->SetScale(1280.0f, 720.0f, 0.0f);
	m_SceneGameObjects.emplace_back(pt->GetGameObject());

	//リザルト文字列オブジェクトを作成
	Texture2D* pt2 = Texture2DFactory::Create(*Game::GetInstance());
	pt2->SetTexture("assets/texture/resultString.png");
	pt2->SetScale(700.0f, 100.0f, 0.0f);
	pt2->SetUV(1, 1, 1, 13);//縦1横13分割の、左から1番目上から5番目を指定
	m_SceneGameObjects.emplace_back(pt2->GetGameObject());

	/*
	// 人オブジェクトを作成
	Texture2D* pt3 = Texture2DFactory::Create(*Game::GetInstance());
	pt3->SetTexture("assets/texture/golf_jou_man.png");
	pt3->SetPosition(-300.0f, 0.0f, 0.0f);
	pt3->SetScale(361.0f, 400.0f, 0.0f);
	m_SceneGameObjects.emplace_back(pt3->GetGameObject());
	*/

}

// 更新
void ResultScene::Update()
{
	// エンターキーを押してタイトルへ
	if (Input::GetKeyTrigger(VK_RETURN))
	{
		Game::GetInstance()->ChangeScene(SceneType::Title);
	}
}

// 終了処理
void ResultScene::Uninit()
{
	// このシーンのオブジェクトを削除する
	for (GameObject* gameObject : m_SceneGameObjects) {
		Game::GetInstance()->DeleteGameObject(gameObject);
	}
	m_SceneGameObjects.clear();
}

// スコアを設定
void ResultScene::SetScore(int c)
{
	// リザルト文字列オブジェクト
	Texture2D* stringObj =
		m_SceneGameObjects[1]->GetComponent<Texture2D>();

	switch (c)
	{
	case -4:
		stringObj->SetUV(1, 2, 1, 13);	// -4 コンドル
		break;
	case -3:
		stringObj->SetUV(1, 3, 1, 13);	// -3 アルバトロス
		break;
	case -2:
		stringObj->SetUV(1, 4, 1, 13);	// -2 イーグル
		break;
	case -1:
		stringObj->SetUV(1, 5, 1, 13);	// -1 バーディ
		break;
	case 0:
		stringObj->SetUV(1, 6, 1, 13);	// 0 パー
		break;
	case 1:
		stringObj->SetUV(1, 7, 1, 13);	// +1 ボギー
		break;
	case 2:
		stringObj->SetUV(1, 8, 1, 13);	// +2 ダブルボギー
		break;
	case 3:
		stringObj->SetUV(1, 9, 1, 13);	// +3 トリプルボギー
		break;
	case 4:
		stringObj->SetUV(1, 10, 1, 13);	// +4
		break;
	case 5:
		stringObj->SetUV(1, 11, 1, 13);	// +5
		break;
	case 6:
		stringObj->SetUV(1, 12, 1, 13);	// +6
		break;
	default:
		stringObj->SetUV(1, 13, 1, 13);	// +7
		break;
	}
}
