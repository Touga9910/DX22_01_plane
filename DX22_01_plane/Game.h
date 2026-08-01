#pragma once
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
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
#include"BattleScene.h"
#include"ResultScene.h"
#include"StageSelectScene.h"
#include"RestSiteScene.h"
#include"ShopScene.h"

#include"SkyBox.h"

#include "input.h"
#include "BallStatus.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "StageSelector.h"
#include "GameObject.h"
#include "SphereColliderComponent.h"
#include "TagComponent.h"

class EnemyBall;
struct EnemyData;
class GameMcpBridge;
class Ground;
class PlayerBall;
class Pocket;
class Pole;
class TableFrame;
class Texture2D;

enum class SceneType {
	Title,
	Select,
	Battle,
	RestSite,
	Shop,
	Result,
	Max
};

enum class RelicType
{
	AllBallAttackUp,
	AllBallDefenseUp,
	CollisionAttackUp,
	Count
};

struct RelicDefinition
{
	RelicType type;
	const char* name;
	const char* description;
	int price;
};

inline constexpr std::array<
	RelicDefinition,
	static_cast<std::size_t>(RelicType::Count)> RelicCatalog =
{{
	{
		RelicType::AllBallAttackUp,
		"Power Core",
		"All owned balls gain +1 ATK.",
		20
	},
	{
		RelicType::AllBallDefenseUp,
		"Guard Core",
		"All owned balls gain +1 DEF.",
		20
	},
	{
		RelicType::CollisionAttackUp,
		"Impact Accelerator",
		"Damage to enemies gains +1 after player-enemy or enemy-enemy hits. Resets each shot.",
		20
	}
}};

enum class DamageBallCollisionType
{
	PlayerEnemy,
	EnemyEnemy
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
	StageSelector m_StageSelector;
	std::optional<StageData> m_McpNextStageOverride;
	std::optional<StageData> m_McpCurrentStageOverride;

	PlayerDeck m_PlayerDeck;
	std::array<bool, static_cast<std::size_t>(RelicType::Count)>
		m_OwnedRelics{};
	int m_CurrentShotCollisionAttackBonus = 0;
	int m_CurrentShotPlayerEnemyCollisionCount = 0;
	int m_CurrentShotEnemyEnemyCollisionCount = 0;
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
	bool m_IsClearRewardChosen = false;
	int m_ClearedStageCount = 0;

	// バランスログ収集用の自動プレイ設定
	bool m_BalanceAutoPlayEnabled = false;
	bool m_AutoRestartAfterGameOver = true;
	int m_AutoDecisionDelayFrames = 20;
	int m_AutoDecisionFrame = 0;
	int m_AutoRunCount = 0;
	int m_AutoMaxRuns = 0;
	float m_AutoMinShotPower = 4.0f;
	float m_AutoMaxShotPower = 8.0f;
	float m_AutoAimJitterDegrees = 1.5f;
	std::mt19937 m_AutoRandomEngine{ std::random_device{}() };
	std::vector<std::uint64_t> m_AutoPendingBallAdjustments;
	std::unique_ptr<GameMcpBridge> m_GameMcpBridge;

	// Dynamic difficulty adjustment (DDA). The result of one battle is
	// applied to enemies spawned in the following battle.
	bool m_DynamicBalanceEnabled = true;
	bool m_DynamicBalanceAppliedEnabled = true;
	int m_DynamicBalanceLevel = 0;
	int m_DynamicBalanceAppliedLevel = 0;
	int m_DynamicBalanceMinLevel = -3;
	int m_DynamicBalanceMaxLevel = 3;
	int m_DynamicBalanceHpStep = 1;
	int m_DynamicBalanceAttackStep = 1;
	int m_DynamicBalanceLevelsPerAttackStep = 2;
	int m_DynamicBalanceMinEnemyHp = 1;
	int m_DynamicBalanceMaxEnemyHp = 100;
	int m_DynamicBalanceMinEnemyAttack = 0;
	int m_DynamicBalanceMaxEnemyAttack = 50;
	float m_DynamicBalanceStrongHpRatio = 0.70f;
	float m_DynamicBalanceWeakHpRatio = 0.30f;
	float m_DynamicBalanceStrongNoHitRate = 0.20f;
	float m_DynamicBalanceWeakNoHitRate = 0.50f;
	float m_DynamicBalanceTargetShotsPerEnemy = 3.0f;
	float m_DynamicBalanceWeakShotMultiplier = 1.5f;
	bool m_DynamicBalanceStageActive = false;
	bool m_DynamicBalanceShotActive = false;
	bool m_DynamicBalanceCurrentShotHit = false;
	int m_DynamicBalanceStageShots = 0;
	int m_DynamicBalanceStageNoHitShots = 0;
	int m_DynamicBalanceStageEnemyCount = 0;
	int m_DynamicBalanceLastLevelChange = 0;
	float m_DynamicBalanceLastHpRatio = 1.0f;
	float m_DynamicBalanceLastNoHitRate = 0.0f;
	float m_DynamicBalanceLastShotsPerEnemy = 0.0f;
	std::string m_DynamicBalanceLastResult = "not_evaluated";
	std::string m_DynamicBalanceLastReason = "No battle has been evaluated.";

	friend class GameMcpBridge;

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
	void ApplyRelicModifiersTo(PlayerBall* player);
	void ResetShotRelicAttackBonus(PlayerBall* player = nullptr);
	void SaveDebugSnapshot();
	void CaptureCurrentPlayerStatus();
	void DrawNextPlayerBall();
	void DiscardCurrentPlayerBall();
	void PrepareNextPlayerBall();
	int CalculateStageRewardMoney() const;
	void CollectStageRewardMoney();
	void LoadBalanceAutoPlayConfig(
		const std::string& filePath =
			"assets/data/balance_autoplay.json");
	void LoadDynamicBalanceConfig(
		const std::string& filePath =
			"assets/data/dynamic_balance.json");
	void FinishDynamicBalanceShot();
	void EvaluateDynamicBalanceStage(bool cleared);
	bool UpdateBalanceAutoPlay();
	bool FireBalanceAutoShot();
	void SelectBalanceAutoBall();
	void ApplyBalanceAutoReward();
	StageType GetBalanceAutoStageType() const;
	bool IsBalanceAutoHealNeeded() const;
	int FindBalanceAutoPendingUpgradeableBall() const;
	int FindBalanceAutoPendingRemovalBall() const;
	void RemoveBalanceAutoPendingBall(std::uint64_t instanceId);
	void PruneBalanceAutoPendingBalls();

public:
	Game(); // コンストラクタ
	~Game(); // デストラクタ

	static void Init(); // 初期化
	static void Update(); // 更新
	static void Draw(); // 描画
	static void Uninit(); // 終了処理

	static Game* GetInstance();
	GameObject* CreateGameObject(const std::string& name);

	void ChangeScene(SceneType sceneType);	//シーンを変更
	void DeleteObject(Object* pt);		//オブジェクトを削除する
	void DeleteComponent(Component* component);
	void DeleteAllObject();				//オブジェクトを全て削除する

	static Camera* GetCamera() { return &m_Instance->m_Camera; }
	static SkyBox* GetSkyBox();

	GameState GetGameState() const { return m_GameState; }
	void SetGameState(GameState state) { m_GameState = state; }
	bool IsBalanceAutoPlayEnabled() const
	{
		return m_BalanceAutoPlayEnabled;
	}

	bool ContainsObject(const Object* pt) const;
	bool ContainsComponent(const Component* component) const;

	void LoadPlayerStatusFromJson(
		const std::string& filePath = "assets/data/player_status.json",
		const std::string& deckFilePath = "assets/data/player_deck.json");
	void ResetPlayerRuntimeStatus();
	void StartNewRun();
	void ApplyPlayerStatusTo(PlayerBall* player);
	void CapturePlayerStatusFrom(const PlayerBall* player);
	void CompleteCurrentStage();
	void StartNextBattle(StageType stageType = StageType::Normal);
	void OnBattleStageStarted(const StageData& stage);
	const StageData* GetCurrentStageOverride() const
	{
		return m_McpCurrentStageOverride.has_value()
			? &m_McpCurrentStageOverride.value()
			: nullptr;
	}
	void OnPlayerShotFired(PlayerBall* player);
	void NotifyDamageBallCollision(DamageBallCollisionType collisionType);
	void NotifyDynamicBalanceHit();
	void ApplyDynamicBalanceToEnemyData(EnemyData& enemyData) const;
	void SetDynamicBalance(
		bool enabled,
		bool resetLevel,
		int requestedLevel,
		bool hasRequestedLevel);
	void NotifyBalanceAutoFullHpEnemySurvived();
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
	int GetPlayerCurrentHp() const { return m_PlayerRunStatus.currentHp; }
	int GetPlayerMaxHp() const { return m_PlayerRunStatus.maxHp; }
	int GetClearedStageCount() const { return m_ClearedStageCount; }
	int GetPlayerProgress() const { return m_PlayerRunStatus.progress; }
	const std::string& GetSelectedStageId() const
	{
		return m_PlayerRunStatus.GetSelectedStageId();
	}

	int GetDeckBallCount() const { return m_PlayerDeck.GetRewardTargetCount(); }
	int GetMinimumDeckSize() const { return PlayerDeck::MinimumDeckSize; }
	const PlayerBallData* GetDeckBall(int index) const { return m_PlayerDeck.GetRewardTarget(index); }
	int GetShopBallCount() const { return m_PlayerDeck.GetCatalogCount(); }
	const PlayerBallData* GetShopBall(int index) const { return m_PlayerDeck.GetCatalogBall(index); }
	bool IsBallAdjustmentCandidate(std::uint64_t instanceId) const;
	bool HasAvailableRestBenefit() const;
	bool RestHeal();
	bool RestUpgradeBall(int ballIndex);
	bool BuyShopBall(int catalogIndex, int cost);
	bool RemoveShopBall(int ballIndex, int cost);
	int GetRelicCount() const
	{
		return static_cast<int>(RelicCatalog.size());
	}
	const RelicDefinition* GetRelic(int index) const
	{
		return index >= 0 && index < GetRelicCount()
			? &RelicCatalog[static_cast<std::size_t>(index)]
			: nullptr;
	}
	bool HasRelic(RelicType type) const
	{
		const std::size_t index = static_cast<std::size_t>(type);
		return index < m_OwnedRelics.size() && m_OwnedRelics[index];
	}
	int GetOwnedRelicCount() const;
	int GetRelicAttackBonus() const;
	int GetRelicDefenseBonus() const;
	int GetCurrentShotCollisionAttackBonus() const
	{
		return m_CurrentShotCollisionAttackBonus;
	}
	int GetCurrentShotPlayerEnemyCollisionCount() const
	{
		return m_CurrentShotPlayerEnemyCollisionCount;
	}
	int GetCurrentShotEnemyEnemyCollisionCount() const
	{
		return m_CurrentShotEnemyEnemyCollisionCount;
	}
	int GetEffectivePlayerBallAttack(const PlayerBallData* ball) const;
	int GetEffectivePlayerBallDefense(const PlayerBallData* ball) const;
	bool BuyRelic(int relicIndex);
};
