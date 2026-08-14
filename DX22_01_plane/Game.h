#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <unordered_map>
#include <vector>
#include <string>

//オブジェクト情報のあるファイルをインクルード
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


#include "input.h"
#include "Camera.h"
#include "BallStatus.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "StageSelector.h"
#include "GameObject.h"
#include "TagComponent.h"
#include "json/json.hpp"

class EnemyBall;
struct EnemyData;
class GameMcpBridge;
class PlayerBall;

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
	BankShot,
	EmergencyRepairKit,
	Count
};

struct RelicDefinition
{
	RelicType type;
	const char* name;
	const char* description;
	int price;
};

struct BalanceValidationVariant
{
	std::string id;
	bool disableDynamicBalance = true;
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
	},
	{
		RelicType::BankShot,
		"Bank Shot",
		"After hitting a wall, the first direct hit against an enemy deals double damage.",
		20
	},
	{
		RelicType::EmergencyRepairKit,
		"Emergency Repair Kit",
		"Recover 1 HP after a shot with at least 3 ball-to-ball contacts.",
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

	//オブジェクト配列
	std::vector<std::unique_ptr<GameObject>> m_GameObjects;

	GameState m_GameState = GameState::AimingDirection;

	BallStatus m_DefaultPlayerStatus{ 10, 1, 0 };
	PlayerRunStatus m_DefaultPlayerRunStatus{};
	PlayerRunStatus m_PlayerRunStatus{};
	float m_RestHealRatio = 0.25f;
	int m_RestHealCooldownBattles = 2;
	int m_RestHealCooldownRemaining = 0;
	StageSelector m_StageSelector;
	std::optional<StageData> m_McpNextStageOverride;
	std::optional<StageData> m_McpCurrentStageOverride;

	PlayerDeck m_PlayerDeck;
	std::array<bool, static_cast<std::size_t>(RelicType::Count)>
		m_OwnedRelics{};
	int m_CurrentShotCollisionAttackBonus = 0;
	int m_CurrentShotPlayerEnemyCollisionCount = 0;
	int m_CurrentShotEnemyEnemyCollisionCount = 0;
	bool m_CurrentShotBankShotReady = false;
	bool m_CurrentShotBankShotConsumed = false;
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
	unsigned int m_AutoRandomSeed = 20260727u;
	std::vector<std::uint64_t> m_AutoPendingBallAdjustments;
	std::unique_ptr<GameMcpBridge> m_GameMcpBridge;
	nlohmann::json m_PendingShotTelemetry = nlohmann::json::object();
	std::uint32_t m_RunRandomSeed = 0;
	std::uint32_t m_StageSelectionSeed = 0;
	std::uint32_t m_RouteSelectionSeed = 0;
	std::uint32_t m_RouteSelectionCounter = 0;
	std::mt19937 m_PocketRandomEngine{ std::random_device{}() };
	std::deque<EnemyBall*> m_PocketedEnemyQueue;
	StageType m_CurrentBattleStageType = StageType::Normal;
	float m_PlayerPocketDamageRatio = 0.05f;
	float m_NormalPocketFinisherRatio = 0.30f;
	float m_MidBossPocketFinisherRatio = 0.20f;
	float m_BossPocketFinisherRatio = 0.10f;
	float m_PlayerPocketReturnHalfWidth = 12.0f;
	float m_PlayerPocketReturnHalfDepth = 8.0f;
	float m_EnemyPocketReturnX = 0.0f;
	float m_EnemyPocketReturnTopEdgeOffset = 10.0f;

	// Fixed-condition balance validation. When enabled, the run seed is fixed
	// and DDA can be forcibly disabled so before/after builds are comparable.
	bool m_BalanceValidationEnabled = false;
	bool m_BalanceValidationDisableDynamicBalance = true;
	bool m_BalanceValidationCurrentDisableDynamicBalance = true;
	bool m_BalanceValidationFixedStageSchedule = true;
	std::uint32_t m_BalanceValidationSeed = 20260807u;
	std::vector<std::uint32_t> m_BalanceValidationSeeds{ 20260807u };
	std::uint32_t m_BalanceValidationRunCounter = 0;
	std::uint32_t m_BalanceValidationSeedIndex = 0;
	std::uint32_t m_BalanceValidationVariantIndex = 0;
	std::string m_BalanceValidationExperimentId = "fixed_baseline";
	std::string m_BalanceValidationCurrentVariantId = "dda_off";
	std::vector<BalanceValidationVariant> m_BalanceValidationVariants{
		{ "dda_off", true },
		{ "dda_on", false },
	};
	int m_BalanceValidationMaximumClearedStages = 30;

	// Baseline difficulty is fixed for the run. DDA remains a separate assist.
	std::string m_BaselineDifficultyProfile = "normal";
	float m_BaselineEnemyHpMultiplier = 1.0f;
	int m_BaselineEnemyAttackDelta = 0;
	bool m_ProgressionScalingEnabled = true;
	int m_ProgressionAttackStart = 20;
	int m_ProgressionAttackInterval = 5;
	int m_ProgressionAttackStep = 1;
	int m_ProgressionAttackMaximumDelta = 8;

	// Encounter threat costs are logged separately from raw enemy count.
	std::unordered_map<std::string, float> m_EnemyThreatCosts;
	std::unordered_map<std::string, float> m_StageThreatTargets;
	float m_StageDataLayoutThreatMultiplier = 1.0f;
	float m_DenseLayoutThreatMultiplier = 1.25f;
	float m_McpLayoutThreatMultiplier = 1.0f;

	// Dynamic difficulty adjustment (DDA). The result of one battle is
	// applied to enemies spawned in the following battle.
	bool m_DynamicBalanceConfiguredEnabled = true;
	bool m_DynamicBalanceEnabled = true;
	bool m_DynamicBalanceAppliedEnabled = true;
	int m_DynamicBalanceInitialLevel = 0;
	int m_DynamicBalanceLevel = 0;
	int m_DynamicBalanceAppliedLevel = 0;
	int m_DynamicBalanceMinLevel = -3;
	int m_DynamicBalanceMaxLevel = 3;
	int m_DynamicBalanceHpStep = 1;
	int m_DynamicBalanceAttackStep = 1;
	int m_DynamicBalanceLevelsPerAttackStep = 2;
	bool m_DynamicBalancePositiveAttackScalingEnabled = false;
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
	void ResetShotRelicState(PlayerBall* player = nullptr);
	void ApplyEndOfShotRelicEffects(PlayerBall* player);
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
	void LoadDifficultyProfileConfig(
		const std::string& filePath =
			"assets/data/difficulty_profiles.json");
	void LoadBalanceValidationConfig(
		const std::string& filePath =
			"assets/data/balance_validation.json");
	void LoadEncounterBalanceConfig(
		const std::string& filePath =
			"assets/data/encounter_balance.json");
	void LoadPocketRulesConfig(
		const std::string& filePath =
			"assets/data/pocket_rules.json");
	void RestoreNextPocketedEnemy();
	DirectX::SimpleMath::Vector3 FindEnemyPocketReturnPosition(
		const EnemyBall* returningEnemy) const;
	void ResetDynamicBalanceRunState();
	StageType GetScheduledStageType() const;
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
	void RemoveDestroyedGameObjects();

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
	void DeleteGameObject(GameObject* gameObject);
	void DeleteAllGameObjects();

	static Camera* GetCamera() { return &m_Instance->m_Camera; }

	GameState GetGameState() const { return m_GameState; }
	void SetGameState(GameState state) { m_GameState = state; }
	bool IsBalanceAutoPlayEnabled() const
	{
		return m_BalanceAutoPlayEnabled;
	}

	bool ContainsGameObject(const GameObject* gameObject) const;
	bool ContainsComponent(const Component* component) const;

	void LoadPlayerStatusFromJson(
		const std::string& filePath = "assets/data/player_status.json",
		const std::string& deckFilePath = "assets/data/player_deck.json");
	void ResetPlayerRuntimeStatus();
	void StartNewRun(
		const std::string& controllerType = std::string(),
		const std::string& controllerProfile = std::string());
	void ApplyPlayerStatusTo(PlayerBall* player);
	void CapturePlayerStatusFrom(const PlayerBall* player);
	void CompleteCurrentStage();
	void StartNextBattle();
	void StartNextBattle(StageType stageType);
	void OnBattleStageStarted(const StageData& stage);
	const StageData* GetCurrentStageOverride() const
	{
		return m_McpCurrentStageOverride.has_value()
			? &m_McpCurrentStageOverride.value()
			: nullptr;
	}
	void OnPlayerShotFired(PlayerBall* player);
	void NotifyPlayerWallCollision();
	int ConsumeBankShotDamageMultiplier();
	void NotifyDamageBallCollision(DamageBallCollisionType collisionType);
	void NotifyPlayerDamage(
		const std::string& source,
		int damage,
		const std::string& sourceId = std::string());
	void HandleEnemyPocket(EnemyBall* enemy);
	DirectX::SimpleMath::Vector3 FindPlayerPocketReturnPosition(
		const PlayerBall* player);
	float GetCurrentPocketFinisherRatio() const;
	bool IsEnemyPocketFinisherEligible(const EnemyBall* enemy) const;
	int GetPocketQueueIndex(const EnemyBall* enemy) const;
	int GetPlayerPocketDamageAmount() const;
	void RecordBalanceEvent(
		const std::string& eventType,
		const nlohmann::json& details = nlohmann::json::object());
	void NotifyDynamicBalanceHit();
	int CalculateDynamicBalanceAttackModifier(int level) const;
	int CalculateProgressionAttackModifier() const;
	void ApplyDynamicBalanceToEnemyData(EnemyData& enemyData) const;
	void SetDynamicBalance(
		bool enabled,
		bool resetLevel,
		int requestedLevel,
		bool hasRequestedLevel);
	void NotifyBalanceAutoFullHpEnemySurvived();
	std::uint32_t GetNextRouteRandomSeed()
	{
		return m_RouteSelectionSeed + m_RouteSelectionCounter++;
	}
	int GetPlayerDeckCount() const { return m_PlayerDeck.GetDrawPileCount(); }
	int GetPlayerDiscardCount() const { return m_PlayerDeck.GetDiscardPileCount(); }

	/// <summary>
	/// 敵が全滅しているかどうかを判定する関数
	bool AreAllEnemiesDefeated() const;

	// Finds all components of the requested type in the world.
	template<typename T> std::vector<T*> GetComponents()
	{
		static_assert(std::is_base_of_v<Component, T>,
			"T must inherit from Component");

		std::vector<T*>res;
		for (auto& gameObject : m_Instance->m_GameObjects)
		{
			if (gameObject->IsDestroyRequested())
			{
				continue;
			}

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
			if (gameObject->IsDestroyRequested())
			{
				continue;
			}

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
			if (gameObject->IsDestroyRequested())
			{
				continue;
			}

			TagComponent* tagComponent = gameObject->GetComponent<TagComponent>();
			if (tagComponent != nullptr && tagComponent->GetTag() == tag)
			{
				result.push_back(gameObject.get());
			}
		}
		return result;
	}

	int GetPlayerMoney() const
	{
		return m_PlayerRunStatus.money;
	}
	int GetPlayerCurrentHp() const { return m_PlayerRunStatus.currentHp; }
	int GetPlayerMaxHp() const { return m_PlayerRunStatus.maxHp; }
	int GetRestHealAmount() const;
	int GetRestHealPercent() const;
	int GetRestHealCooldownBattles() const
	{
		return m_RestHealCooldownBattles;
	}
	int GetRestHealCooldownRemaining() const
	{
		return m_RestHealCooldownRemaining;
	}
	bool CanRestHeal() const
	{
		return m_RestHealCooldownRemaining <= 0 &&
			m_PlayerRunStatus.currentHp < m_PlayerRunStatus.maxHp;
	}
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
	int GetCurrentShotBallCollisionCount() const
	{
		return m_CurrentShotPlayerEnemyCollisionCount +
			m_CurrentShotEnemyEnemyCollisionCount;
	}
	bool IsCurrentShotBankShotReady() const
	{
		return m_CurrentShotBankShotReady &&
			!m_CurrentShotBankShotConsumed;
	}
	int GetEffectivePlayerBallAttack(const PlayerBallData* ball) const;
	int GetEffectivePlayerBallDefense(const PlayerBallData* ball) const;
	bool BuyRelic(int relicIndex);
};
