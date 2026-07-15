#pragma once
#include <iostream>
#include <vector>

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
	std::vector<std::unique_ptr<Object>> m_Objects;

	GameState m_GameState = GameState::AimingDirection;

	BallStatus m_DefaultPlayerStatus{ 10, 1, 0 };
	PlayerRunStatus m_DefaultPlayerRunStatus{};
	PlayerRunStatus m_PlayerRunStatus{};

	PlayerDeck m_PlayerDeck;

	// ==========================
	// 報酬UI用
	// ==========================
	int m_SelectedRewardIndex = 0;
	int m_SelectedRewardBallIndex = 0;

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

	/// <summary>
	/// 報酬を適用する関数
	/// </summary>
	void ApplyReward(int rewardIndex);
	void ApplyPlayerRunStatusTo(PlayerBall* player);
	void SaveDebugSnapshot();
	void CaptureCurrentPlayerStatus();
	void DrawNextPlayerBall();
	void DiscardCurrentPlayerBall();
	void PrepareNextPlayerBall();

public:
	Game(); // コンストラクタ
	~Game(); // デストラクタ

	static void Init(); // 初期化
	static void Update(); // 更新
	static void Draw(); // 描画
	static void Uninit(); // 終了処理

	static Game* GetInstance();

	void ChangeScene(SceneName sName);	//シーンを変更
	void DeleteObject(Object* pt);		//オブジェクトを削除する
	void DeleteAllObject();				//オブジェクトを全て削除する

	static Camera* GetCamera() { return &m_Instance->m_Camera; }
	static SkyBox* GetSkyBox();

	GameState GetGameState() const { return m_GameState; }
	void SetGameState(GameState state) { m_GameState = state; }

	bool ContainsObject(const Object* pt) const;

	void LoadPlayerStatusFromJson(const std::string& filePath = "assets/data/player_status.json");
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
		T* pt = new T;
		m_Instance->m_Objects.emplace_back(pt);
		pt->Init();
		return pt;
	}

	//オブジェクトを取得する
	template<typename T>std::vector<T*> GetObjects()
	{
		std::vector<T*>res;
		for (auto& o : m_Instance->m_Objects)
		{
			//dynamic_castで型をチェック
			if (T* derivedObj = dynamic_cast<T*>(o.get()))
			{
				res.emplace_back(derivedObj);
			}
		}
		return res;
	}

	//オブジェクトを追加する.座標指定版
	template<typename T> T* AddObjectWithPosition(DirectX::SimpleMath::Vector3 pos)
	{
		T* pt = new T;
		pt->SetInitPosition(pos); // Init前に座標をセット
		m_Instance->m_Objects.emplace_back(pt);
		pt->Init();               // 座標セット後にInit
		return pt;
	}
};
