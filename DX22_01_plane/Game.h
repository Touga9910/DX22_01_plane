#pragma once
#include <iostream>
#include <memory>
#include <vector>
#include <string>
#include <typeinfo>

//オブジェクト情報のあるファイルをインクルード
//#include "TestPlane.h"
//#include "TestCube.h"
//#include "TestGolfFlag.h"
//#include "TestModel.h"
//#include "
// .h"
//#include "Ground.h"


#include"Renderer.h"
#include"TitleScene.h"
#include"Stage1Scene.h"
#include"Stage2Scene.h"
#include"Stage3Scene.h"
#include"ResultScene.h"
#include"StageSelectScene.h"

#include"SkyBox.h"

#include "input.h"
#include "BallStatus.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "GameObject.h"
#include "SphereColliderComponent.h"
#include "TagComponent.h"

class EnemyBall;
class Ground;
class PlayerBall;
class Pocket;
class Pole;
class TableFrame;
class Texture2D;

enum SceneName {
	TITLE,
	SELECT,
	STAGE1,
	STAGE2,
	STAGE3,
	RESULT,
	SCENE_MAX
};

// ゲーム全体のターン進行状態
enum class GameState {
	AimingDirection,	// 方向選択中
	AimingPower,		// パワー選択中
	ConfirmShot,		// ショット確認・弾道表示中
	BallsMoving,		// ボール移動中
	EnemyAttack,		// 敵の攻撃中
	TurnEnd,			// ターン終了（翌フレームに AimingDirection へ自動遷移）

	ClearReward,		// クリア時の報酬表示
	GameOver,			// ゲームオーバー
};

class Game
{
private:
	static Game* m_Instance;//ゲームインスタンス

	Scene* m_Scene;//シーン

	// カメラ
	Camera&  m_Camera = Camera::GetInstance();

	// スカイボックス
	SkyBox* m_SkyBox = nullptr;

	//オブジェクト配列
	std::vector<std::unique_ptr<GameObject>> m_GameObjects;

	GameState m_GameState = GameState::AimingDirection;

	BallStatus m_DefaultPlayerStatus{ 10, 1, 0 };
	PlayerRunStatus m_DefaultPlayerRunStatus{};
	PlayerRunStatus m_PlayerRunStatus{};

	PlayerDeck m_PlayerDeck;
	int m_SelectedOfferIndex = 0;
	int m_SelectedHoldIndex = -1;

	// ==========================
	// 報酬・ショップUI用
	// ==========================
	int m_SelectedRewardIndex = 0;
	int m_SelectedRewardBallIndex = 0;

	int m_CurrentStageRewardMoney = 0;    // 今回のステージで取得したMoney
	bool m_IsStageRewardCollected = false; // 二重取得防止
	std::string m_RewardMessage;           // 購入結果などの表示

	/// <summary>
	/// 全てのボールが停止しているかどうかを判定する関数
	/// </summary>
	bool AreAllBallsStopped() const;

	/// <summary>
	/// 敵の攻撃処理を行う関数
	/// </summary>
	void ProcessEnemyAttack();

	/// <summary>
	/// ゲームオーバー時の処理を行う関数
	/// </summary>
	void ProcessGameOver();

	/// <summary>
	/// 敵全滅時に報酬UIを表示する処理
	/// </summary>
	void StartClearReward();

	/// <summary>
	/// 報酬UIの更新処理を行う関数
	/// </summary>
	void UpdateClearReward();

	/// <summary>
	/// 報酬UIの描画処理を行う関数
	/// </summary>
	void DrawClearRewardUI();
	void BeginBallSelection();
	void UpdateBallSelection();
	void DrawBallSelectionUI();
	void ApplySelectedBallPreview();

	/// <summary>
	/// 報酬を適用する関数
	/// </summary>
	void ApplyPlayerRunStatusTo(PlayerBall* player);
	void SaveDebugSnapshot();
	void CaptureCurrentPlayerStatus();
	void DrawNextPlayerBall();
	void DiscardCurrentPlayerBall();
	void PrepareNextPlayerBall();
	int CalculateStageRewardMoney() const;
	void CollectStageRewardMoney();
	bool TryPurchaseReward(int rewardIndex);
	bool ApplyReward(int rewardIndex);

public:
	Game(); // コンストラクタ
	~Game(); // デストラクタ

	static void Init(); // 初期化
	static void Update(); // 更新
	static void Draw(); // 描画
	static void Uninit(); // 終了処理

	static Game* GetInstance();
	GameObject* CreateGameObject(const std::string& name);

	void ChangeScene(SceneName sName);	//シーンを変更
	void DeleteObject(Object* pt);		//オブジェクトを削除する
	void DeleteComponent(Component* component);
	void DeleteAllObject();				//オブジェクトを全て削除する

	static Camera* GetCamera() { return &m_Instance->m_Camera; }
	static SkyBox* GetSkyBox();

	GameState GetGameState() const { return m_GameState; }
	void SetGameState(GameState state) { m_GameState = state; }

	bool ContainsObject(const Object* pt) const;
	bool ContainsComponent(const Component* component) const;

	void LoadPlayerStatusFromJson(
		const std::string& filePath = "assets/data/player_status.json",
		const std::string& deckFilePath = "assets/data/player_deck.json");
	void ResetPlayerRuntimeStatus();
	void ApplyPlayerStatusTo(PlayerBall* player);
	void CapturePlayerStatusFrom(const PlayerBall* player);
	void OnPlayerShotFired(PlayerBall* player);
	int GetPlayerDeckCount() const { return m_PlayerDeck.GetDrawPileCount(); }
	int GetPlayerDiscardCount() const { return m_PlayerDeck.GetDiscardPileCount(); }

	/// <summary>
	/// 敵が全滅しているかどうかを判定する関数
	bool AreAllEnemiesDefeated() const;

	//オブジェクトを追加する（※テンプレート関数）
	template<typename T> T* AddObject()
	{
		static_assert(std::is_base_of_v<Component, T>,
			"T must inherit from Component");

		GameObject* gameObject = CreateGameObject(typeid(T).name());
		T* component = gameObject->AddComponent<T>();

		GameObjectTag tag = GameObjectTag::None;
		if constexpr (std::is_same_v<T, Ground>) tag = GameObjectTag::Ground;
		else if constexpr (std::is_same_v<T, TableFrame>) tag = GameObjectTag::Rail;
		else if constexpr (std::is_same_v<T, Pocket>) tag = GameObjectTag::Pocket;
		else if constexpr (std::is_same_v<T, Pole>) tag = GameObjectTag::Goal;
		else if constexpr (std::is_same_v<T, Texture2D>) tag = GameObjectTag::ScreenUi;

		if (tag != GameObjectTag::None)
		{
			gameObject->AddComponent<TagComponent>(tag);
		}

		if constexpr (std::is_same_v<T, Pocket>)
		{
			gameObject->AddComponent<SphereColliderComponent>(2.0f, true);
		}
		return component;
	}

	//オブジェクトを取得する
	template<typename T>std::vector<T*> GetObjects()
	{
		static_assert(std::is_base_of_v<Component, T>,
			"T must inherit from Component");

		std::vector<T*>res;
		for (auto& gameObject : m_Instance->m_GameObjects)
		{
			if (T* component = gameObject->GetComponent<T>())
			{
				res.emplace_back(component);
			}
		}
		return res;
	}

	// Entityの継承型ではなく、保持ComponentでWorldを検索する。
	template<typename T>
	std::vector<GameObject*> GetGameObjectsWith()
	{
		static_assert(std::is_base_of_v<Component, T>,
			"T must inherit from Component");

		std::vector<GameObject*> result;
		for (auto& gameObject : m_Instance->m_GameObjects)
		{
			if (gameObject->HasComponent<T>())
			{
				result.push_back(gameObject.get());
			}
		}
		return result;
	}

	std::vector<GameObject*> GetGameObjectsWithTag(GameObjectTag tag)
	{
		std::vector<GameObject*> result;
		for (auto& gameObject : m_Instance->m_GameObjects)
		{
			TagComponent* tagComponent = gameObject->GetComponent<TagComponent>();
			if (tagComponent != nullptr && tagComponent->GetTag() == tag)
			{
				result.push_back(gameObject.get());
			}
		}
		return result;
	}

	//オブジェクトを追加する.座標指定版
	template<typename T> T* AddObjectWithPosition(DirectX::SimpleMath::Vector3 pos)
	{
		T* component = AddObject<T>();
		component->SetInitPosition(pos);
		return component;
	}

	int GetPlayerMoney() const
	{
		return m_PlayerRunStatus.money;
	}
};
