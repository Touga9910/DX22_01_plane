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
#include "GroundRenderComponent.h"
#include "GameObject.h"
#include "TagComponent.h"
#include "TableFrame.h"
#include "TableFrameCollisionComponent.h"
#include "TableFrameRenderComponent.h"
#include "PocketFactory.h"
#include "Pocket.h"
//#include "Arrow.h"

#include "Texture2D.h"
#include "Texture2DFactory.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <typeinfo>
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
	Game* game = Game::GetInstance();
	PlayerBall* ball = BallFactory::CreatePlayer(*game);
	m_SceneGameObjects.emplace_back(ball->GetGameObject());

	GameObject* groundObject = game->CreateGameObject(typeid(Ground).name());
	groundObject->AddComponent<TagComponent>(GameObjectTag::Ground);
	groundObject->AddComponent<GroundRenderComponent>();
	Ground* ground = groundObject->AddComponent<Ground>();
	m_SceneGameObjects.emplace_back(groundObject);

	GameObject* tableFrameObject =
		game->CreateGameObject(typeid(TableFrame).name());
	tableFrameObject->AddComponent<TagComponent>(GameObjectTag::Rail);
	tableFrameObject->AddComponent<TableFrameCollisionComponent>();
	tableFrameObject->AddComponent<TableFrameRenderComponent>();
	TableFrame* tableFrame = tableFrameObject->AddComponent<TableFrame>();
	m_SceneGameObjects.emplace_back(tableFrameObject);

	for (const Collision::Sphere& pocketSphere :
		tableFrame->GetPocketSpheres())
	{
		Pocket* pocket = PocketFactory::Create(
			*game,
			pocketSphere.center,
			pocketSphere.radius);
		m_SceneGameObjects.emplace_back(pocket->GetGameObject());
	}

	// ========================================================
	// エネミー（EnemyBall）の出現処理
	// ========================================================
	const StageData* stageData = game->GetCurrentStageOverride();
	const bool usesMcpStageOverride = stageData != nullptr;
	std::vector<StageData> stages;
	if (!usesMcpStageOverride)
	{
		stages = StageDataLoader::LoadAll(
			m_StageJsonPath,
			m_EnemyJsonPath
		);
	}

	m_LastStageJsonWriteTime = GetJsonWriteTime(m_StageJsonPath);
	m_LastEnemyJsonWriteTime = GetJsonWriteTime(m_EnemyJsonPath);

	m_SelectedStageId = game->GetSelectedStageId();
	if (!usesMcpStageOverride)
	{
		stageData =
			StageDataLoader::FindById(stages, m_SelectedStageId);
	}
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
		StageData adjustedStage = *stageData;
		for (EnemySpawnData& spawn : adjustedStage.enemies)
		{
			Game::GetInstance()->ApplyDynamicBalanceToEnemyData(
				spawn.enemyData);
		}
		if (!usesMcpStageOverride)
		{
			ArrangeDenseEnemySpawns(adjustedStage);
		}

		m_Par = adjustedStage.par;
		ValidateEnemySpawns(adjustedStage, *ball, *tableFrame);

		for (const EnemySpawnData& spawn : adjustedStage.enemies)
		{
			EnemyData enemyData = spawn.enemyData;
			enemyData.initPosition = spawn.position;
			enemyData.initPosition.y = TableConfig::FIELD_HEIGHT;

			EnemyBall* enemy =
				BallFactory::CreateEnemy(*Game::GetInstance(), enemyData);
			m_SceneGameObjects.emplace_back(enemy->GetGameObject());
		}

		Game::GetInstance()->OnBattleStageStarted(adjustedStage);
	}

	std::cout << "\nオブジェクトの生成終了\n" << std::endl;

	// UIの作成
	/*
	{
		// UI（背景）
		Texture2D* pt1 = Texture2DFactory::Create(*Game::GetInstance());
		pt1->SetTexture("assets/texture/ui_back.png");	// 画像指定
		pt1->SetPosition(-475.0f, -300.0f, 0.0f);		// 位置指定
		pt1->SetScale(270.0f, 75.0f, 0.0f);				// 大きさ指定
		m_SceneGameObjects.emplace_back(pt1->GetGameObject());

		// UI（「パー」）
		Texture2D* pt2 = Texture2DFactory::Create(*Game::GetInstance());
		pt2->SetTexture("assets/texture/ui_string.png");// 画像指定
		pt2->SetPosition(-575.0f, -240.0f, 0.0f);		// 位置指定
		pt2->SetScale(60.0f, 45.0f, 0.0f);				// 大きさ指定
		pt2->SetUV(1, 1, 2, 1);							// UV指定
		m_SceneGameObjects.emplace_back(pt2->GetGameObject());

		// UI（「打目」）
		Texture2D* pt3 = Texture2DFactory::Create(*Game::GetInstance());
		pt3->SetTexture("assets/texture/ui_string.png");// 画像指定
		pt3->SetPosition(-400.0f, -305.0f, 0.0f);		// 位置指定
		pt3->SetScale(105.0f, 63.0f, 0.0f);				// 大きさ指定
		pt3->SetUV(2, 1, 2, 1);							// UV指定
		m_SceneGameObjects.emplace_back(pt3->GetGameObject());

		// UI（パーの数値）
		Texture2D* pt4 = Texture2DFactory::Create(*Game::GetInstance());
		pt4->SetTexture("assets/texture/ui_number.png");// 画像指定
		pt4->SetPosition(-510.0f, -245.0f, 0.0f);		// 位置指定
		pt4->SetScale(65.0f, 45.0f, 0.0f);				// 大きさ指定
		pt4->SetUV((float)(m_Par + 1), 1, 10, 1);			// UV指定
		m_SceneGameObjects.emplace_back(pt4->GetGameObject());

		// UI（現在打数の数値 一の位）
		Texture2D* pt5 = Texture2DFactory::Create(*Game::GetInstance());
		pt5->SetTexture("assets/texture/ui_number.png");// 画像指定
		pt5->SetPosition(-485.0f, -300.0f, 0.0f);		// 位置指定
		pt5->SetScale(95.0f, 72.0f, 0.0f);				// 大きさ指定
		pt5->SetUV(2, 1, 10, 1);				// UV指定
		m_SceneGameObjects.emplace_back(pt5->GetGameObject());

		// UI（現在打数の数値 十の位）
		Texture2D* pt6 = Texture2DFactory::Create(*Game::GetInstance());
		pt6->SetTexture("assets/texture/ui_number.png");// 画像指定
		pt6->SetPosition(-556.0f, -300.0f, 0.0f);		// 位置指定
		pt6->SetScale(95.0f, 72.0f, 0.0f);				// 大きさ指定
		pt6->SetUV(1, 1, 10, 1);				// UV指定
		m_SceneGameObjects.emplace_back(pt6->GetGameObject());
	}
	*/

	ball->SetState(PlayerBall::State::Simulation);	//ボールを物理挙動させる
	//arrow->SetState(0);	//矢印非表示
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
	if (Game::GetInstance()->GetCurrentStageOverride() != nullptr)
	{
		return;
	}

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
		EnemyData enemyData = spawn.enemyData;
		Game::GetInstance()->ApplyDynamicBalanceToEnemyData(
			enemyData);
		enemyDataMap[enemyData.id] = std::move(enemyData);
	}

	std::vector<EnemyBall*> enemies =
		Game::GetInstance()->GetComponents<EnemyBall>();

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

void BattleScene::ArrangeDenseEnemySpawns(StageData& stage) const
{
	constexpr size_t kDenseColumnCount = 3;
	constexpr float kDenseGap = 2.0f;
	constexpr float kNearestRowDistance = 14.0f;

	if (stage.enemies.size() <= 3)
	{
		return;
	}

	float maxRadius = 0.01f;
	float originalCenterZ = 0.0f;
	for (const EnemySpawnData& spawn : stage.enemies)
	{
		maxRadius = (std::max)(
			maxRadius,
			spawn.enemyData.status.radius);
		originalCenterZ += spawn.position.z;
	}
	originalCenterZ /= static_cast<float>(stage.enemies.size());

	const float spacing =
		maxRadius * 2.0f + kDenseGap;
	const float zDirection =
		originalCenterZ < 0.0f ? -1.0f : 1.0f;

	size_t enemyIndex = 0;
	size_t rowIndex = 0;
	while (enemyIndex < stage.enemies.size())
	{
		const size_t enemiesInRow = (std::min)(
			kDenseColumnCount,
			stage.enemies.size() - enemyIndex);
		const float rowStartX =
			-static_cast<float>(enemiesInRow - 1) *
			spacing * 0.5f;
		const float rowZ =
			zDirection *
			(kNearestRowDistance +
			 static_cast<float>(rowIndex) * spacing);

		for (size_t columnIndex = 0;
			columnIndex < enemiesInRow;
			columnIndex++, enemyIndex++)
		{
			EnemySpawnData& spawn = stage.enemies[enemyIndex];
			spawn.position = Vector3(
				rowStartX +
					static_cast<float>(columnIndex) * spacing,
				TableConfig::FIELD_HEIGHT,
				rowZ);
			spawn.enemyData.initPosition = spawn.position;
		}

		rowIndex++;
	}
}

void BattleScene::ValidateEnemySpawns(
	const StageData& stage,
	const PlayerBall& player,
	const TableFrame& tableFrame) const
{
	const float fieldHalfWidth = TableConfig::GetFieldWidth() * 0.5f;
	const float fieldHalfDepth = TableConfig::GetFieldDepth() * 0.5f;
	const std::vector<Collision::Segment> walls = tableFrame.GetWalls();
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
