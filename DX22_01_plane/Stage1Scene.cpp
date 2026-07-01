#include "Stage1Scene.h"
#include "Game.h"
#include "Input.h"
#include "PlayerBall.h"
#include "EnemyBall.h"
#include "Ground.h"
//#include "Arrow.h"
#include "Pole.h"
#include "SkyBox.h"

#include "Texture2D.h"
#include <iostream>


using namespace DirectX::SimpleMath;

// コンストラクタ
Stage1Scene::Stage1Scene()
{
	Init();
}

// 初期化
void Stage1Scene::Init()
{
	m_Par = 4;			// パーを設定
	m_StrokeCount = 0;	// 現在打数を初期化

	std::cout << "オブジェクトを生成開始\n" << std::endl;

	// オブジェクトを作成
	m_MySceneObjects.emplace_back(Game::GetInstance()->AddObject<PlayerBall>());
	m_MySceneObjects.emplace_back(Game::GetInstance()->AddObject<Ground>());
	//m_MySceneObjects.emplace_back(Game::GetInstance()->AddObject<Arrow>());	//矢印
	m_MySceneObjects.emplace_back(Game::GetInstance()->AddObject<Pole>());	//ポール
	
	// ========================================================
	// ★ エネミー（EnemyBall）の出現処理
	// ========================================================
	// 例として、ステージ1に3体のエネミーをそれぞれ違う位置に出現させます
	Vector3 enemyPositions[] = {
		Vector3(50.0f,  0.0f, 50.0f),
		Vector3(-50.0f, 0.0f, 60.0f),
		Vector3(0.0f,   0.0f, 50.0f),
		Vector3(50.0f,   0.0f, 40.0f),
		Vector3(50.0f,   0.0f, 30.0f),
		Vector3(50.0f,   0.0f, 20.0f),
		Vector3(50.0f,   0.0f, 10.0f),
		Vector3(50.0f,   0.0f, 0.0f),
		Vector3(50.0f,   0.0f, -10.0f),
	};
	/*
	for (const auto& pos : enemyPositions)
	{
		EnemyBall* enemy = Game::GetInstance()->AddObject<EnemyBall>();
		enemy->GetTransform().position = pos; // 座標を上書き
		m_MySceneObjects.emplace_back(enemy);
	}
	*/

	for (const auto& pos : enemyPositions)
	{
		EnemyBall* enemy = Game::GetInstance()->AddObjectWithPosition<EnemyBall>(pos);
		m_MySceneObjects.emplace_back(enemy);
	}

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


	PlayerBall* ball = dynamic_cast<PlayerBall*>(m_MySceneObjects[0]);//ゴルフボール
	//Arrow* arrow = dynamic_cast<Arrow*>(m_MySceneObjects[2]);//矢印
	Pole* pole = dynamic_cast<Pole*>(m_MySceneObjects[2]);//ポール
	ball->SetState(PlayerBall::State::Simulation);	//ボールを物理挙動させる
	//arrow->SetState(0);	//矢印非表示
	pole->SetPosition(200.0f,-25.0f,0.0f);	//ポールを設定
}

//更新
void Stage1Scene::Update()
{
	StageBase::Update();
}