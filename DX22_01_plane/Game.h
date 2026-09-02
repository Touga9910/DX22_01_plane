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
#include "GameTypes.h"
#include "GameEvent.h"
#include "RunResultSnapshot.h"
#include "RunStatisticsTracker.h"
#include "SettingsManager.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "StageSelector.h"
#include "GameObject.h"
#include "TagComponent.h"
#include "json/json.hpp"

class EnemyBall;
struct EnemyData;
class GameMcpBridge;
class GamePresentation;
class GameSaveManager;
class PlayerBall;

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
	int m_CurrentShotWallCollisionCount = 0;
	int m_CurrentShotBounceDamageBonus = 0;
	bool m_CurrentShotAnchorStopped = false;
	float m_CurrentShotLaunchPower = 0.0f;
	bool m_BountyRewardClaimed = false;
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
	bool m_IsMidBossRelicSelectionActive = false;
	int m_SelectedRelicOfferIndex = 0;
	std::vector<int> m_MidBossRelicOffers;
	std::vector<int> m_ShopRelicOffers;
	bool m_ShopRelicPurchased = false;
	int m_ClearedStageCount = 0;
	static constexpr int kNormalRouteAreaGoal = 15;
	int m_AreaProgress = 0;
	RunPhase m_RunPhase = RunPhase::NormalRoute;

	// バランスログ収集用の自動プレイ設定
	bool m_BalanceAutoPlayEnabled = false;
	bool m_AutoRestartAfterGameOver = true;
	int m_AutoDecisionDelayFrames = 20;
	int m_AutoDecisionFrame = 0;
	int m_AllBallsStoppedFrameCount = 0;
	int m_AutoRunCount = 0;
	int m_AutoMaxRuns = 0;
	float m_AutoMinShotPower = 4.0f;
	float m_AutoMaxShotPower = 8.0f;
	float m_AutoAimJitterDegrees = 1.5f;
	std::mt19937 m_AutoRandomEngine{ std::random_device{}() };
	unsigned int m_AutoRandomSeed = 20260727u;
	std::vector<std::uint64_t> m_AutoPendingBallAdjustments;
	std::unique_ptr<GameMcpBridge> m_GameMcpBridge;
	std::unique_ptr<GamePresentation> m_GamePresentation;
	nlohmann::json m_PendingShotTelemetry = nlohmann::json::object();
	std::uint32_t m_RunRandomSeed = 0;
	std::uint32_t m_StageSelectionSeed = 0;
	std::uint32_t m_RouteSelectionSeed = 0;
	std::uint32_t m_RouteSelectionCounter = 0;
	std::mt19937 m_PocketRandomEngine{ std::random_device{}() };
	std::mt19937 m_RelicRandomEngine{ std::random_device{}() };
	std::deque<EnemyBall*> m_PocketedEnemyQueue;
	StageType m_CurrentBattleStageType = StageType::Normal;
	float m_PlayerPocketDamageRatio = 0.04f;
	float m_NormalPocketFinisherRatio = 0.30f;
	float m_MidBossPocketFinisherRatio = 0.20f;
	float m_BossPocketFinisherRatio = 0.10f;
	float m_PlayerPocketReturnHalfWidth = 12.0f;
	float m_PlayerPocketReturnHalfDepth = 8.0f;
	float m_EnemyPocketReturnX = 0.0f;
	float m_EnemyPocketReturnTopEdgeOffset = 10.0f;

	// 固定条件でバランスを検証する。有効時はランのシードを固定し、
	// DDAを強制的に無効化できるため、変更前後のビルドを比較できる。
	bool m_BalanceValidationEnabled = false;
	bool m_BalanceValidationDisableDynamicBalance = true;
	bool m_BalanceValidationCurrentDisableDynamicBalance = true;
	bool m_BalanceValidationFixedStageSchedule = true;
	bool m_BalanceValidationEnduranceMode = false;
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

	// 基準難易度はラン中に固定し、DDAは独立した救済機能として扱う。
	std::string m_BaselineDifficultyProfile = "normal";
	float m_BaselineEnemyHpMultiplier = 1.0f;
	int m_BaselineEnemyAttackDelta = 0;
	bool m_ProgressionScalingEnabled = true;
	int m_ProgressionHpStart = 10;
	int m_ProgressionHpInterval = 5;
	int m_ProgressionHpStep = 1;
	int m_ProgressionHpMaximumDelta = 4;
	int m_ProgressionAttackStart = 20;
	int m_ProgressionAttackInterval = 5;
	int m_ProgressionAttackStep = 1;
	int m_ProgressionAttackMaximumDelta = 8;

	// エンカウントの脅威度コストは、単純な敵数とは分けてログへ記録する。
	std::unordered_map<std::string, float> m_EnemyThreatCosts;
	std::unordered_map<std::string, float> m_StageThreatTargets;
	float m_StageDataLayoutThreatMultiplier = 1.0f;
	float m_DenseLayoutThreatMultiplier = 1.25f;
	float m_McpLayoutThreatMultiplier = 1.0f;

	// 動的難易度調整（DDA）。1戦の結果を、次の戦闘で生成する敵へ適用する。
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
	bool m_IsRestoringRunSave = false;
	std::string m_SaveLoadMessage;
	int m_SaveLoadMessageFrames = 0;
	RunStatisticsTracker m_RunStatistics{};
	SettingsManager m_SettingsManager{};
	bool m_RunActive = false;
	bool m_IsPaused = false;
	bool m_PauseConfirmTitle = false;
	bool m_PendingDisplayApply = false;
	RunResultSnapshot m_LastRunResult{};

	struct DebugEnemyCombatSnapshot
	{
		std::string id;
		int currentHp = 0;
		int maxHp = 0;
		int attack = 0;
		int damageBeforeMinimum = 0;
		int expectedDamage = 0;
		bool minimumDamageApplied = false;
		bool defeated = false;
		bool pocketed = false;
		bool canAttack = false;
		std::string state;
	};

	struct DebugCombatForecastSnapshot
	{
		bool hasPlayer = false;
		int playerCurrentHp = 0;
		int playerMaxHp = 0;
		int playerDefense = 0;
		int theoreticalDamage = 0;
		int expectedDamage = 0;
		int overkillDamage = 0;
		int attackerCount = 0;
		int hpAfterAttack = 0;
		bool lethal = false;
		std::vector<DebugEnemyCombatSnapshot> enemies;
		std::uint64_t updateRevision = 0;
		std::string updateReason = "initial";
	};

	struct DebugPlayerDamageRecord
	{
		std::uint64_t sequence = 0;
		std::string source;
		std::string sourceId;
		int damage = 0;
		int hpBefore = -1;
		int hpAfter = -1;
	};

	DebugCombatForecastSnapshot m_DebugCombatForecast{};
	bool m_DebugCombatForecastDirty = true;
	std::string m_DebugCombatForecastPendingReason = "initial";
	std::deque<DebugPlayerDamageRecord> m_DebugPlayerDamageHistory;
	std::uint64_t m_DebugDamageSequence = 0;
	bool m_DebugLastEnemyAttackComparisonValid = false;
	int m_DebugLastEnemyAttackPredictedDamage = 0;
	int m_DebugLastEnemyAttackActualDamage = 0;
	bool m_DebugShowDefeatedEnemies = true;
	bool m_DebugShowPocketedEnemies = true;
	bool m_DebugOnlyAttackers = false;
	int m_DebugEnemySortMode = 0;

	friend class GameMcpBridge;
	friend class GamePresentation;
	friend class GameSaveManager;

	/// <summary>
	/// 全てのボールが停止しているかどうかを判定する関数
	/// </summary>
	bool AreAllBallsStopped() const;
	bool TryRecoverClearedBattle(const char* source);

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
	std::vector<int> RollRelicOffers(int count, bool midBoss);
	bool GrantRelic(int relicIndex, const char* source);
	bool IsCurrentBall(const char* definitionId) const;
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
	void RestorePocketedPlayer();
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
	int GetClearRewardUpgradeCost(int ballIndex) const;
	bool ApplyClearRewardUpgrade(int ballIndex, int& chargedCost);
	StageType GetBalanceAutoStageType() const;
	bool IsBalanceAutoHealNeeded() const;
	int FindBalanceAutoRelicToBuy() const;
	int FindBalanceAutoWeakestBall() const;
	int FindBalanceAutoMissingCatalogBall() const;
	int FindBalanceAutoUpgradeTarget() const;
	bool HasBalanceAutoShopAction() const;
	int FindBalanceAutoPendingUpgradeableBall() const;
	int FindBalanceAutoPendingRemovalBall() const;
	void RemoveBalanceAutoPendingBall(std::uint64_t instanceId);
	void PruneBalanceAutoPendingBalls();
	void RemoveDestroyedGameObjects();
	bool SaveRunCheckpoint(
		SceneType sceneType,
		bool sceneAlreadyActive,
		bool showNotification);
	void SetSaveLoadMessage(const std::string& message);
	bool CanPause() const;
	bool SaveAndReturnToTitle();
	void DrawPauseUI();
	void FinalizeRunResult(bool completed);
	void CompleteNormalRouteArea(const char* areaType);
	void EnterNextRouteAfterArea();
	void CompleteFinalBossRun();
	void PublishGameEvent(const GameEvent& event);
	void RefreshDebugCombatForecast();
	void RecordDebugPlayerDamage(
		const std::string& source,
		const std::string& sourceId,
		int damage,
		int hpBefore,
		int hpAfter);

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
		const std::string& controllerProfile = std::string(),
		const std::string& buildProfile = std::string(),
		const std::string& buildProfileSettingsHash = std::string(),
		std::optional<std::uint32_t> forcedRandomSeed = std::nullopt,
		const std::string& forcedValidationVariant = std::string());
	bool HasValidRunSave() const;
	std::string GetRunSaveSummary() const;
	const std::string& GetSaveLoadMessage() const
	{
		return m_SaveLoadMessage;
	}
	bool SaveCurrentRun();
	bool LoadSavedRun();
	const RunResultSnapshot& GetRunStatistics() const
	{
		return m_RunStatistics.GetState();
	}
	bool WasLastRunCompleted() const
	{
		return m_LastRunResult.completed;
	}
	const RunResultSnapshot& GetLastRunResult() const
	{
		return m_LastRunResult;
	}
	const GameSettings& GetSettings() const
	{
		return m_SettingsManager.Get();
	}
	bool IsVibrationEnabled() const
	{
		return m_SettingsManager.Get().vibrationEnabled;
	}
	bool IsScreenFlashEnabled() const
	{
		return m_SettingsManager.Get().screenFlashEnabled;
	}
	bool IsCameraShakeEnabled() const
	{
		return m_SettingsManager.Get().cameraShakeEnabled;
	}
	void ApplyPlayerStatusTo(PlayerBall* player);
	void CapturePlayerStatusFrom(const PlayerBall* player);
	void CompleteCurrentStage();
	void LeaveShop();
	void LeaveRestSite();
	void ContinueAfterClearReward();
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
	void NotifyAnchorStopped();
	void NotifyEnemyDefeated(const std::string& enemyId);
	int ConsumeBankShotDamageMultiplier();
	int ConsumePlayerEnemyRelicDamageBonus();
	int GetPierceMaximumUses() const;
	float GetPierceSpeedRetention() const;
	void NotifyDamageBallCollision(DamageBallCollisionType collisionType);
	void NotifyCombatFeedback(
		const DirectX::SimpleMath::Vector3& worldPosition,
		int damage,
		bool defeated,
		bool enemyEnemyCollision);
	void NotifyPocketFeedback(
		const DirectX::SimpleMath::Vector3& worldPosition,
		bool playerPocket,
		bool finisher,
		int damage = 0);
	void NotifyPlayerDamage(
		const std::string& source,
		int damage,
		const std::string& sourceId = std::string(),
		int hpBefore = -1,
		int hpAfter = -1);
	void InvalidateDebugCombatForecast(const char* reason);
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
	int CalculateProgressionHpModifier() const;
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

	// ゲーム空間から、指定した型のコンポーネントをすべて取得する。
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
	bool CanRestHeal() const
	{
		return m_PlayerRunStatus.currentHp < m_PlayerRunStatus.maxHp;
	}
	int GetClearedStageCount() const { return m_ClearedStageCount; }
	int GetAreaProgress() const { return m_AreaProgress; }
	int GetNormalRouteAreaGoal() const { return kNormalRouteAreaGoal; }
	RunPhase GetRunPhase() const { return m_RunPhase; }
	bool IsBossPreparation() const
	{
		return m_RunPhase == RunPhase::BossPreparation;
	}
	bool IsFinalBossRoute() const
	{
		return m_RunPhase == RunPhase::FinalBossReady;
	}
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
	int GetBallOfferSize() const
	{
		return HasRelic(RelicType::ExpandedBallOffer) ? 4 : 3;
	}
	void RollShopRelicOffers();
	int GetShopRelicOfferCount() const
	{
		return static_cast<int>(m_ShopRelicOffers.size());
	}
	int GetShopRelicOfferCatalogIndex(int offerIndex) const
	{
		return offerIndex >= 0 && offerIndex < GetShopRelicOfferCount()
			? m_ShopRelicOffers[static_cast<std::size_t>(offerIndex)]
			: -1;
	}
	const RelicDefinition* GetShopRelicOffer(int offerIndex) const
	{
		return GetRelic(GetShopRelicOfferCatalogIndex(offerIndex));
	}
	bool IsShopRelicOffered(int relicIndex) const;
	bool HasPurchasedShopRelic() const { return m_ShopRelicPurchased; }
	bool BuyShopRelicOffer(int offerIndex);
	bool BuyShopRelic(int relicIndex);
	void RollMidBossRelicOffers();
	int GetMidBossRelicOfferCount() const
	{
		return static_cast<int>(m_MidBossRelicOffers.size());
	}
	int GetMidBossRelicOfferCatalogIndex(int offerIndex) const
	{
		return offerIndex >= 0 && offerIndex < GetMidBossRelicOfferCount()
			? m_MidBossRelicOffers[static_cast<std::size_t>(offerIndex)]
			: -1;
	}
	const RelicDefinition* GetMidBossRelicOffer(int offerIndex) const
	{
		return GetRelic(GetMidBossRelicOfferCatalogIndex(offerIndex));
	}
	bool IsMidBossRelicSelectionActive() const
	{
		return m_IsMidBossRelicSelectionActive;
	}
	bool AcquireMidBossRelicOffer(int offerIndex);
	bool AcquireMidBossRelic(int relicIndex);
	bool BuyRelic(int relicIndex);
};
