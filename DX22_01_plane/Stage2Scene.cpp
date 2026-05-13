#include "Stage2Scene.h"
#include "Game.h"
#include "Input.h"
#include "GolfBall.h"
#include "Ground.h"
#include "Arrow.h"
#include "Pole.h"
#include "SkyBox.h"

#include "Texture2D.h"


using namespace DirectX::SimpleMath;

// コンストラクタ
Stage2Scene::Stage2Scene()
{
	Init();
}

// デストラクタ
Stage2Scene::~Stage2Scene()
{
	Uninit();
}

// 初期化
void Stage2Scene::Init()
{
	m_Par = 4;			// パーを設定
	m_StrokeCount = 0;	// 現在打数を初期化

	std::cout << "オブジェクトを生成開始\n" << std::endl;

	// オブジェクトを作成
	m_MySceneObjects.emplace_back(Game::GetInstance()->AddObject<GolfBall>());
	m_MySceneObjects.emplace_back(Game::GetInstance()->AddObject<Ground>());
	m_MySceneObjects.emplace_back(Game::GetInstance()->AddObject<Arrow>());	//矢印
	m_MySceneObjects.emplace_back(Game::GetInstance()->AddObject<Pole>());	//ポール
	m_MySceneObjects.emplace_back(Game::GetInstance()->AddObject<SkyBox>());	//ポール

	std::cout << "\nオブジェクトの生成終了\n" << std::endl;

	// UIの作成
	{
		// UI（背景）
		Texture2D* pt1 = Game::GetInstance()->AddObject<Texture2D>();
		pt1->SetTexture("assets/texture/ui_back.png");	// 画像指定
		pt1->SetPosition(-475.0f, -300.0f, 0.0f);		// 位置指定
		pt1->SetScale(270.0f, 75.0f, 0.0f);				// 大きさ指定
		m_MySceneObjects.emplace_back(pt1);//m_MySceneObjects[4]

		// UI（「パー」）
		Texture2D* pt2 = Game::GetInstance()->AddObject<Texture2D>();
		pt2->SetTexture("assets/texture/ui_string.png");// 画像指定
		pt2->SetPosition(-575.0f, -240.0f, 0.0f);		// 位置指定
		pt2->SetScale(60.0f, 45.0f, 0.0f);				// 大きさ指定
		pt2->SetUV(1, 1, 2, 1);							// UV指定
		m_MySceneObjects.emplace_back(pt2);//m_MySceneObjects[5]

		// UI（「打目」）
		Texture2D* pt3 = Game::GetInstance()->AddObject<Texture2D>();
		pt3->SetTexture("assets/texture/ui_string.png");// 画像指定
		pt3->SetPosition(-400.0f, -305.0f, 0.0f);		// 位置指定
		pt3->SetScale(105.0f, 63.0f, 0.0f);				// 大きさ指定
		pt3->SetUV(2, 1, 2, 1);							// UV指定
		m_MySceneObjects.emplace_back(pt3);//m_MySceneObjects[6]

		// UI（パーの数値）
		Texture2D* pt4 = Game::GetInstance()->AddObject<Texture2D>();
		pt4->SetTexture("assets/texture/ui_number.png");// 画像指定
		pt4->SetPosition(-510.0f, -245.0f, 0.0f);		// 位置指定
		pt4->SetScale(65.0f, 45.0f, 0.0f);				// 大きさ指定
		pt4->SetUV((float)(m_Par + 1), 1, 10, 1);			// UV指定
		m_MySceneObjects.emplace_back(pt4);//m_MySceneObjects[7]

		// UI（現在打数の数値 一の位）
		Texture2D* pt5 = Game::GetInstance()->AddObject<Texture2D>();
		pt5->SetTexture("assets/texture/ui_number.png");// 画像指定
		pt5->SetPosition(-485.0f, -300.0f, 0.0f);		// 位置指定
		pt5->SetScale(95.0f, 72.0f, 0.0f);				// 大きさ指定
		pt5->SetUV(2, 1, 10, 1);				// UV指定
		m_MySceneObjects.emplace_back(pt5);//m_MySceneObjects[8]

		// UI（現在打数の数値 十の位）
		Texture2D* pt6 = Game::GetInstance()->AddObject<Texture2D>();
		pt6->SetTexture("assets/texture/ui_number.png");// 画像指定
		pt6->SetPosition(-556.0f, -300.0f, 0.0f);		// 位置指定
		pt6->SetScale(95.0f, 72.0f, 0.0f);				// 大きさ指定
		pt6->SetUV(1, 1, 10, 1);				// UV指定
		m_MySceneObjects.emplace_back(pt6);//m_MySceneObjects[8]
	}


	GolfBall* ball = dynamic_cast<GolfBall*>(m_MySceneObjects[0]);//ゴルフボール
	Arrow* arrow = dynamic_cast<Arrow*>(m_MySceneObjects[2]);//矢印
	Pole* pole = dynamic_cast<Pole*>(m_MySceneObjects[3]);//ポール
	ball->SetState(0);	//ボールを物理挙動させる
	arrow->SetState(0);	//矢印非表示
	pole->SetPosition(0.0f, -25.0f, 0.0f);	//ポールを設定





}

//更新
void Stage2Scene::Update()
{
	GolfBall* ball = dynamic_cast<GolfBall*>(m_MySceneObjects[0]);//ゴルフボール
	Arrow* arrow = dynamic_cast<Arrow*>(m_MySceneObjects[2]);//矢印

	//状態ごとに処理
	switch (m_State)
	{
		//ボール移動中
	case 0:
		//ボールが静止したら
		if (ball->GetState() == 1)
		{
			m_State = 1;
			arrow->SetState(m_State);

			// 打数を更新
			Texture2D* count[2] = {};
			count[0] = dynamic_cast<Texture2D*>(m_MySceneObjects[8]);//打数 一の位
			count[1] = dynamic_cast<Texture2D*>(m_MySceneObjects[9]);//打数 十の位

			m_StrokeCount++;//現在打数をカウントアップ

			// 各桁を後ろから取得していく
			for (int i = 0; i < 2; i++)
			{
				int cnt = m_StrokeCount % (int)pow(10, i + 1) / (int)pow(10, i);//一桁取り出す
				count[i]->SetUV((float)(cnt + 1), 1, 10, 1);//UV設定
			}
		}
		//ボールがカップインしたらリザルトへ
		else if (ball->GetState() == 2)
		{
			Game::GetInstance()->ChangeScene(RESULT);
		}
		break;
		//スペースキーでパワー選択
	case 1:
		if (Input::GetKeyTrigger(VK_SPACE))
		{
			m_State = 2;
			arrow->SetState(m_State);
		}
		break;
		//スペースキーでショット
	case 2:
		if (Input::GetKeyTrigger(VK_SPACE))
		{
			m_State = 3;
			arrow->SetState(m_State);
		}
		break;
	case 3:
		if (Input::GetKeyTrigger(VK_SPACE))
		{
			m_State = 0;
			ball->SetState(m_State);
			arrow->SetState(m_State);

			Vector3 v = arrow->GetVector();
			ball->Shot(v);
		}
		break;

	default:
		break;
	}

	/*
	// エンターキーを押してリザルトへ
	if (Input::GetKeyTrigger(VK_RETURN))
	{
		Game::GetInstance()->ChangeScene(RESULT);
	}
	*/
}

// 終了処理
void Stage2Scene::Uninit()
{
	// このシーンのオブジェクトを削除する
	for (auto& o : m_MySceneObjects) {
		Game::GetInstance()->DeleteObject(o);
	}
	m_MySceneObjects.clear();
}

// スコアを取得
int Stage2Scene::GetScore() const
{
	// 現在打数から標準打数（パー）を引いた値をreturn
	return(m_StrokeCount - m_Par);
}