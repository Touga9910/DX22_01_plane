#include "BattleScene.h"
#include "Game.h"
#include "Input.h"
#include "PlayerBall.h"
#include "EnemyBall.h"
#include "BallFactory.h"
#include "EnemyData.h"
#include "StageDataLoader.h"
#include "TableConfig.h"
#include "Collision.h"

#include "Ground.h"
#include "TableFrame.h"
#include "Pocket.h"
//#include "Arrow.h"
#include "Pole.h"
#include "SkyBox.h"

#include "Texture2D.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_map>

using namespace DirectX::SimpleMath;

// コンストラクタ
BattleScene::BattleScene()
{
	Init();
}

// 初期化
void BattleScene::Init()
{
	m_Par = 4;			// パーを設定
	m_StrokeCount = 0;	// 現在打数を初期化

	std::cout << "オブジェクトを生成開始\n" << std::endl;

	// オブジェクトを作成
	PlayerBall* ball = BallFactory::CreatePlayer(*Game::GetInstance());
	m_MySceneObjects.emplace_back(ball);
	m_MySceneObjects.emplace_back(Game::GetInstance()->AddObject<Ground>());
	//m_MySceneObjects.emplace_back(Game::GetInstance()->AddObject<Arrow>());	//矢印
	Pole* pole = Game::GetInstance()->AddObject<Pole>();
	m_MySceneObjects.emplace_back(pole);	//ポール


	TableFrame* tableFrame = Game::GetInstance()->AddObject<TableFrame>();
	m_MySceneObjects.emplace_back(tableFrame);
	m_MySceneObjects.emplace_back(
		Game::GetInstance()->AddObject<SkyBox>());

	// ========================================================
	// TableFrame のポケット位置を元に Pocket を生成
	// ========================================================
	std::vector<Collision::Sphere> pocketSpheres = tableFrame->GetPocketSpheres();

	for (const Collision::Sphere& sphere : pocketSpheres)
	{
		Pocket* pocket = Game::GetInstance()->AddObject<Pocket>();
		pocket->SetPosition(sphere.center);
		pocket->SetRadius(sphere.radius);

		m_MySceneObjects.emplace_back(pocket);
	}

	// ========================================================
	// エネミー（EnemyBall）の出現処理
	// ========================================================
	std::vector<StageData> stages = StageDataLoader::LoadAll(
		m_StageJsonPath,
		m_EnemyJsonPath
	);

	m_LastStageJsonWriteTime = GetJsonWriteTime(m_StageJsonPath);
	m_LastEnemyJsonWriteTime = GetJsonWriteTime(m_EnemyJsonPath);

	m_SelectedStageId = Game::GetInstance()->GetSelectedStageId();
	const StageData* stageData =
		StageDataLoader::FindById(stages, m_SelectedStageId);
	if (stageData == nullptr)
	{
		std::cerr << "[BattleScene] 保存済みstage ID「"
			<< m_SelectedStageId << "」が見つかりません" << std::endl;

		if (!stages.empty())
		{
			stageData = &stages.front();
			m_SelectedStageId = stageData->id;
			std::cerr << "[BattleScene] 安全なフォールバックとして「"
				<< m_SelectedStageId << "」を使用します" << std::endl;
		}
	}

	if (stageData != nullptr)
	{
		m_Par = stageData->par;
		ValidateEnemySpawns(*stageData, *ball, *tableFrame);

		for (const EnemySpawnData& spawn : stageData->enemies)
		{
			EnemyData enemyData = spawn.enemyData;
			enemyData.initPosition = spawn.position;
			enemyData.initPosition.y = TableConfig::FIELD_HEIGHT;

			EnemyBall* enemy =
				BallFactory::CreateEnemy(*Game::GetInstance(), enemyData);
			m_MySceneObjects.emplace_back(enemy);
		}
	}

	std::cout << "\nオブジェクトの生成終了\n" << std::endl;

	// UIの作成
	/*
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
	*/

	//Arrow* arrow = dynamic_cast<Arrow*>(m_MySceneObjects[2]);//矢印
	ball->SetState(PlayerBall::State::Simulation);	//ボールを物理挙動させる
	//arrow->SetState(0);	//矢印非表示
	pole->SetPosition(200.0f,-25.0f,0.0f);	//ポールを設定
}

//更新
void BattleScene::Update()
{
	StageBase::Update();

	UpdateJsonHotReload();
	if (Input::GetKeyTrigger(VK_F5))
	{
		ReloadEnemyStatusFromJson();
	}
}

// =======================================
// JSONホットリロード関連の処理
// =======================================
void BattleScene::UpdateJsonHotReload()
{
	// すでに変更検知済みなら、少し待ってからリロードする
	if (m_HotReloadPending)
	{
		m_HotReloadWaitFrame++;

		if (m_HotReloadWaitFrame < 30)
		{
			return;
		}

		m_HotReloadPending = false;
		m_HotReloadWaitFrame = 0;

		std::cout << "[HotReload] JSONを再読み込みします" << std::endl;
		ReloadEnemyStatusFromJson();
		return;
	}

	m_HotReloadCheckFrame++;

	// 毎フレームではなく、60フレームに1回だけ確認
	if (m_HotReloadCheckFrame < 60)
	{
		return;
	}

	m_HotReloadCheckFrame = 0;

	auto currentStageWriteTime = GetJsonWriteTime(m_StageJsonPath);
	auto currentEnemyWriteTime = GetJsonWriteTime(m_EnemyJsonPath);

	bool stageChanged =
		currentStageWriteTime != m_LastStageJsonWriteTime;

	bool enemyChanged =
		currentEnemyWriteTime != m_LastEnemyJsonWriteTime;

	if (!stageChanged && !enemyChanged)
	{
		return;
	}

	// ここではまだ読み込まない
	// 更新時刻だけ保存して、少し待ってから読む
	m_LastStageJsonWriteTime = currentStageWriteTime;
	m_LastEnemyJsonWriteTime = currentEnemyWriteTime;

	m_HotReloadPending = true;
	m_HotReloadWaitFrame = 0;

	std::cout << "[HotReload] JSON変更を検知しました。少し待ってから再読み込みします。"
		<< std::endl;
}

void BattleScene::ReloadEnemyStatusFromJson()
{
	std::vector<StageData> stages = StageDataLoader::LoadAll(
		m_StageJsonPath,
		m_EnemyJsonPath
	);

	const StageData* stageData =
		StageDataLoader::FindById(stages, m_SelectedStageId);
	if (stageData == nullptr)
	{
		std::cerr << "[HotReload] 現在のstage ID「" << m_SelectedStageId
			<< "」が再読込後のJSONに存在しないため、反映を中止しました"
			<< std::endl;
		return;
	}

	if (stageData->enemies.empty())
	{
		std::cout << "[HotReload] 敵データが空のため、反映を中止しました"
			<< std::endl;
		return;
	}

	std::unordered_map<std::string, EnemyData> enemyDataMap;

	for (const EnemySpawnData& spawn : stageData->enemies)
	{
		enemyDataMap[spawn.enemyData.id] = spawn.enemyData;
	}

	std::vector<EnemyBall*> enemies =
		Game::GetInstance()->GetObjects<EnemyBall>();

	for (EnemyBall* enemy : enemies)
	{
		if (enemy == nullptr)
		{
			continue;
		}

		auto it = enemyDataMap.find(enemy->GetEnemyId());

		if (it == enemyDataMap.end())
		{
			continue;
		}

		enemy->ApplyHotReloadData(it->second);
	}

	std::cout << "[HotReload] 敵ステータスを更新しました" << std::endl;
}

void BattleScene::ValidateEnemySpawns(
	const StageData& stage,
	const PlayerBall& player,
	const TableFrame& tableFrame) const
{
	const float fieldHalfWidth = TableConfig::GetFieldWidth() * 0.5f;
	const float fieldHalfDepth = TableConfig::GetFieldDepth() * 0.5f;
	const std::vector<Collision::Segment> walls = tableFrame.GetWalls();
	const std::vector<Collision::Sphere> pockets =
		tableFrame.GetPocketSpheres();
	const Collision::Sphere playerSphere = player.GetBall()->GetSphere();
	std::vector<Collision::Sphere> validatedEnemies;

	auto warn = [&stage](
		size_t enemyIndex,
		const Vector3& position,
		const char* issue)
	{
		std::cerr << "[StageValidation] Stage ID: " << stage.id
			<< " / Enemy Index: " << enemyIndex
			<< " / Position: (" << position.x << ", "
			<< position.y << ", " << position.z << ")"
			<< " / Issue: " << issue << std::endl;
	};

	for (size_t enemyIndex = 0;
		enemyIndex < stage.enemies.size();
		++enemyIndex)
	{
		const EnemySpawnData& spawn = stage.enemies[enemyIndex];
		Vector3 position = spawn.position;
		position.y = TableConfig::FIELD_HEIGHT;
		const float radius =
			(std::max)(0.01f, spawn.enemyData.status.radius);
		const Collision::Sphere enemySphere{ position, radius };

		if (std::abs(position.x) + radius > fieldHalfWidth ||
			std::abs(position.z) + radius > fieldHalfDepth)
		{
			warn(enemyIndex, position, "プレイ可能範囲外");
		}

		for (const Collision::Segment& wall : walls)
		{
			Vector3 wallCheckPosition = position;
			wallCheckPosition.y = wall.start.y;
			if (Collision::DistancePointToSegment(
				wallCheckPosition,
				wall) <= radius)
			{
				warn(enemyIndex, position, "壁と重なっています");
				break;
			}
		}

		for (const Collision::Sphere& pocket : pockets)
		{
			if (Collision::CheckHit(enemySphere, pocket))
			{
				warn(enemyIndex, position, "ポケットと重なっています");
				break;
			}
		}

		if (Collision::CheckHit(enemySphere, playerSphere))
		{
			warn(enemyIndex, position, "プレイヤー初期位置と重なっています");
		}

		for (const Collision::Sphere& otherEnemy : validatedEnemies)
		{
			if (Collision::CheckHit(enemySphere, otherEnemy))
			{
				warn(enemyIndex, position, "別の敵と重なっています");
				break;
			}
		}

		validatedEnemies.push_back(enemySphere);
	}
}

std::filesystem::file_time_type BattleScene::GetJsonWriteTime(
	const std::string& path
) const
{
	try
	{
		if (std::filesystem::exists(path))
		{
			return std::filesystem::last_write_time(path);
		}
	}
	catch (const std::exception& e)
	{
		std::cout << "[HotReload] JSON更新時刻の取得に失敗: "
			<< path << std::endl;
		std::cout << e.what() << std::endl;
	}

	return {};
}
