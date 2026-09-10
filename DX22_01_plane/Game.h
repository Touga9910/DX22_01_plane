#pragma once
#include "ShotRelicRules.h"
#include "DebugBattleSetup.h"
#include "GameWorld.h"
#include "BattleController.h"
#include "SceneManager.h"
#include "RunProgressController.h"
#include "StageLayoutEditor.h"
#include "ProgressionProfile.h"
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
#include "FixedStepClock.h"
#include "TagComponent.h"
#include "json/json.hpp"

class EnemyBall;
struct EnemyData;
class GameMcpBridge;
class GamePresentation;
class GameSaveManager;
class PlayerBall;
class Scene;

class Game
{
private:
	static Game* m_Instance;//ゲームインスタンス

	SceneManager m_SceneManager;

	// カメラ
	Camera&  m_Camera = Camera::GetInstance();

	GameWorld m_World;

	BattleController m_BattleController;

	// 直近に確定した戦闘結果。
	// MCPやResult側から参照できるようGameが履歴だけ保持する。
	BattleResult m_LastBattleResult = BattleResult::None;

	// ClearRewardは戦闘内部状態ではないためGame側で保持する。
	bool m_IsClearRewardActive = false;

	FixedStepClock m_PhysicsClock;
	bool m_ResetPhysicsElapsed = true;
	std::uint64_t m_PhysicsTickCount = 0;
	int m_PhysicsStepsLastFrame = 0;
	int m_PhysicsSubstepsLastTick = 0;
	std::uint64_t m_PhysicsSubstepLimitCount = 0;
	void UpdateFixedPhysics(double elapsedSeconds);
	void InitializeBattleController();

	BallStatus m_DefaultPlayerStatus{};
	PlayerRunStatus m_DefaultPlayerRunStatus{};
	PlayerRunStatus m_PlayerRunStatus{};
	float m_RestHealRatio = 0.25f;
	StageSelector m_StageSelector;
	std::optional<StageData> m_McpNextStageOverride;
	std::optional<StageData> m_McpCurrentStageOverride;

	PlayerDeck m_PlayerDeck;
    nlohmann::json m_BossShotCache;
    std::string m_BossShotCacheKey;
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
	bool m_ClearRewardMouseConfirmed = false;
	bool m_IsMidBossRelicSelectionActive = false;
	int m_SelectedRelicOfferIndex = 0;
	std::vector<int> m_MidBossRelicOffers;
	std::vector<int> m_ShopRelicOffers;
	static constexpr int kNormalRouteAreaGoal =
		RunProgressController::NormalRouteAreaGoal;
	RunProgressController m_RunProgress;

	// バランスログ収集用の自動プレイ設定
	bool m_BalanceAutoPlayEnabled = false;
	bool m_AutoRestartAfterGameOver = true;
	bool m_AutoStopAfterCurrentRunDefault = false;
	bool m_AutoStopAfterCurrentRunRequested = false;
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
	ProgressionProfile m_ProgressionProfile{};
	int m_ActiveAscension = 0;
	bool m_PersistentProgressEligible = false;
	float m_DefaultRestHealRatio = 0.25f;
	std::vector<std::string> m_LastProgressionUnlocks;
	SettingsManager m_SettingsManager{};
	bool m_RunActive = false;
	bool m_DebugMode = false;
	bool m_DebugEditorOpen = false;
	bool m_DebugBattleFinished = false;
	bool m_DebugPreviousAutoPlay = false;
	bool m_DebugPreviousValidation = false;
	int m_DebugRequest = 0; // 1:開始・再戦、2:タイトルへ
	DebugBattleSetup m_DebugSetup;
	DebugBattleSetup m_DebugActiveSetup;
	std::vector<PlayerBallData> m_DebugBallCatalog;
	std::vector<EnemyData> m_DebugEnemyCatalog;
	std::vector<StageData> m_DebugStages;
	std::string m_DebugMessage;
	void DrawDebugMode();
	StageLayoutEditor m_StageEditor;
	void DrawStageEditor();
	void TestStageEditorLayout();
	float StageEditorPlayerRadius() const;
	bool UpdateDebugMode();
	bool StartDebugBattle();
	void EndDebugMode();
	void FinishDebugBattle(bool victory);
	void ApplyDebugRunSettings();
	void SaveDebugPreset();
	bool LoadDebugPreset();
	bool m_IsPaused = false;
	bool m_MousePauseToggle = false;
	bool m_MouseSaveRequested = false;
	bool m_MouseFullscreenToggle = false;
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
	friend class GameSaveManager;

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
	bool BeginBallSelection();
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
	ShotRelicRules CaptureShotRelicRules() const;
	void CommitShotRelicRules(const ShotRelicRules& rules);
	bool IsCurrentBall(const char* definitionId) const;
	void ResetShotRelicState(PlayerBall* player = nullptr);
	void ApplyEndOfShotRelicEffects(PlayerBall* player);
	void SaveDebugSnapshot();
	void CaptureCurrentPlayerStatus();
	void DrawNextPlayerBall();
	void DiscardCurrentPlayerBall();
	bool PrepareNextPlayerBall();
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
	void RecordPersistentProgress();
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
	void OpenDebugMode();
	// デバッグモードが有効かを返す。
	bool IsDebugMode() const { return m_DebugMode; }
	void ApplyDebugBattlePlayer(PlayerBall* player);
	void ApplyDebugBattleEnemy(EnemyBall* enemy, std::size_t index);
	Game(); // コンストラクタ
	~Game(); // デストラクタ

	static void Init(); // 初期化
	static void Update(double elapsedSeconds); // 入力・表示フレームの更新
	static void ResetFrameTiming();
	static void Draw(); // 描画
	static void Uninit(); // 終了処理

	static Game* GetInstance();
	GameObject* CreateGameObject(const std::string& name);

	void ChangeScene(SceneType sceneType);	//シーンを変更
	// 現在のシーンを読み取り用に返す。
	Scene* GetCurrentScene() const { return m_SceneManager.Get(); }
	// 現在のシーン種別を返す。
	SceneType GetCurrentSceneType() const { return m_SceneManager.GetType(); }
	void DeleteGameObject(GameObject* gameObject);
	void DeleteAllGameObjects();

	// ゲームで使用するカメラを返す。
	static Camera* GetCamera() { return &m_Instance->m_Camera; }

	// 現在ポーズメニューを開けるかを返す。
	bool CanOpenPauseMenu() const { return CanPause(); }
	// 次回更新時のポーズ切り替えを要求する。
	void RequestPauseToggle() { m_MousePauseToggle = true; }
	// 次回更新時の手動セーブを要求する。
	void RequestManualSave() { m_MouseSaveRequested = true; }
	// 次回更新時の全画面切り替えを要求する。
	void RequestFullscreenToggle() { m_MouseFullscreenToggle = true; }
	// ゲームがポーズ中かを返す。
	bool IsPaused() const { return m_IsPaused; }
	// BattleStateを取得する
	BattleState GetBattleState() const
	{
		return m_BattleController.GetState();
	}

	BattleResult GetBattleResult() const
	{
		return m_BattleController.GetResult();
	}

	BattleResult GetLastBattleResult() const
	{
		return m_LastBattleResult;
	}

	bool IsBattleActive() const
	{
		return m_BattleController.IsActive();
	}

	bool IsClearRewardActive() const
	{
		return m_IsClearRewardActive;
	}

	bool AreAllBallsStopped() const
	{
		return m_BattleController.AreAllBallsStopped();
	}
	// バランス検証用の自動プレイが有効かを返す。
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
	// 直近のセーブ・ロード結果メッセージを返す。
	const std::string& GetSaveLoadMessage() const
	{
		return m_SaveLoadMessage;
	}
	bool SaveCurrentRun();
	bool LoadSavedRun();
	// 現在のラン統計を読み取り専用で返す。
	const RunResultSnapshot& GetRunStatistics() const
	{
		return m_RunStatistics.GetState();
	}
    nlohmann::json EvaluateBossShots();
    bool FireBossPlannedShot(const std::string& candidateId, const std::string& stateKey);
	// 直近のランが完走扱いかを返す。
	bool WasLastRunCompleted() const
	{
		return m_LastRunResult.completed;
	}
	// 直近に確定したラン結果を返す。
	const RunResultSnapshot& GetLastRunResult() const
	{
		return m_LastRunResult;
	}
	// 永続進行データを読み取り専用で返す。
	const ProgressionProfile& GetProgressionProfile() const { return m_ProgressionProfile; }
	// 直近に解放された要素の一覧を返す。
	const std::vector<std::string>& GetLastProgressionUnlocks() const { return m_LastProgressionUnlocks; }
	// 現在適用中のアセンション値を返す。
	int GetActiveAscension() const { return m_ActiveAscension; }
	void SetSelectedAscension(int level);
	// 指定したボールが永続解放済みかを返す。
	bool IsBallPermanentlyUnlocked(const std::string& id) const { return m_ProgressionProfile.IsBallUnlocked(id); }
	// 指定したレリックが永続解放済みかを返す。
	bool IsRelicPermanentlyUnlocked(RelicType type) const { return m_ProgressionProfile.IsRelicUnlocked(type); }
	// 現在のゲーム設定を読み取り専用で返す。
	const GameSettings& GetSettings() const
	{
		return m_SettingsManager.Get();
	}
	// 振動設定が有効かを返す。
	bool IsVibrationEnabled() const
	{
		return m_SettingsManager.Get().vibrationEnabled;
	}
	// 画面フラッシュ設定が有効かを返す。
	bool IsScreenFlashEnabled() const
	{
		return m_SettingsManager.Get().screenFlashEnabled;
	}
	// カメラ揺れ設定が有効かを返す。
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
	// MCPで上書きされた現在のステージ情報を返す。
	const StageData* GetCurrentStageOverride() const
	{
		return m_McpCurrentStageOverride.has_value()
			? &m_McpCurrentStageOverride.value()
			: nullptr;
	}
	void OnPlayerShotFired(PlayerBall* player);
	void NotifyBattleAimDirectionStarted();
	void NotifyBattlePowerSelectionStarted();
	void NotifyBattleShotConfirmed();
	void NotifyBattleShotCancelled();
	void NotifyBattlePlayerDefeated();	
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
	// 次回のルート選択に使う一意な乱数シードを返す。
	std::uint32_t GetNextRouteRandomSeed()
	{
		return m_RouteSelectionSeed + m_RouteSelectionCounter++;
	}
	// 山札に残っているボール数を返す。
	int GetPlayerDeckCount() const { return m_PlayerDeck.GetDrawPileCount(); }
	// 捨て札にあるボール数を返す。
	int GetPlayerDiscardCount() const { return m_PlayerDeck.GetDiscardPileCount(); }

	/// <summary>
	/// 敵が全滅しているかどうかを判定する関数
	bool AreAllEnemiesDefeated() const;

	// ゲーム空間から、指定した型のコンポーネントをすべて取得する。
	template<typename T> std::vector<T*> GetComponents()
	{
		return m_World.GetComponents<T>();
	}

	// Entityの継承型ではなく、保持ComponentでWorldを検索する。
	template<typename T>
	std::vector<GameObject*> GetGameObjectsWith()
	{
		return m_World.GetObjectsWith<T>();
	}

	// 指定したタグを持つゲームオブジェクトを取得する。
	std::vector<GameObject*> GetGameObjectsWithTag(GameObjectTag tag)
	{
		return m_World.GetObjectsWithTag(tag);
	}

	// プレイヤーの所持金を返す。
	int GetPlayerMoney() const
	{
		return m_PlayerRunStatus.money;
	}
	// プレイヤーの現在HPを返す。
	int GetPlayerCurrentHp() const { return m_PlayerRunStatus.currentHp; }
	// プレイヤーの最大HPを返す。
	int GetPlayerMaxHp() const { return m_PlayerRunStatus.maxHp; }
	int GetRestHealAmount() const;
	int GetRestHealPercent() const;
	// 休憩による回復を利用できるかを返す。
	bool CanRestHeal() const
	{
		return m_PlayerRunStatus.currentHp < m_PlayerRunStatus.maxHp;
	}
	// 現在までにクリアした戦闘数を返す。
	int GetClearedStageCount() const
	{
		return m_RunProgress.GetClearedBattleCount();
	}
	// 通常ルートの現在進行数を返す。
	int GetAreaProgress() const { return m_RunProgress.GetAreaProgress(); }
	// 現在のランマップを読み取り専用で返す。
	const RunMap& GetRunMap() const { return m_RunProgress.GetMap(); }
	// 選択可能なマップノードを選ぶ。
	bool ChooseMapNode(int nodeId) { return m_RunProgress.ChooseNode(nodeId); }
	// 通常ルートの完了目標エリア数を返す。
	int GetNormalRouteAreaGoal() const { return kNormalRouteAreaGoal; }
	// 現在のランフェーズを返す。
	RunPhase GetRunPhase() const { return m_RunProgress.GetPhase(); }
	// 最終ボス前の準備フェーズかを返す。
	bool IsBossPreparation() const
	{
		return m_RunProgress.IsBossPreparation();
	}
	// 最終ボスを選択可能なフェーズかを返す。
	bool IsFinalBossRoute() const
	{
		return m_RunProgress.IsFinalBossReady();
	}
	// プレイヤー表示用の現在進行値を返す。
	int GetPlayerProgress() const { return m_PlayerRunStatus.progress; }
	// 現在選択されているステージIDを返す。
	const std::string& GetSelectedStageId() const
	{
		return m_PlayerRunStatus.GetSelectedStageId();
	}

	// 報酬対象となるデッキ内ボール数を返す。
	int GetDeckBallCount() const { return m_PlayerDeck.GetRewardTargetCount(); }
	// デッキの最小構成数を返す。
	int GetMinimumDeckSize() const { return PlayerDeck::MinimumDeckSize; }
	// 指定位置のデッキ内ボールを返す。
	const PlayerBallData* GetDeckBall(int index) const { return m_PlayerDeck.GetRewardTarget(index); }
	// ショップの商品候補となるボール数を返す。
	int GetShopBallCount() const { return m_PlayerDeck.GetCatalogCount(); }
	// 指定位置のショップ用ボール情報を返す。
	const PlayerBallData* GetShopBall(int index) const { return m_PlayerDeck.GetCatalogBall(index); }
	bool IsBallAdjustmentCandidate(std::uint64_t instanceId) const;
	bool HasAvailableRestBenefit() const;
	bool RestHeal();
	bool RestUpgradeBall(int ballIndex);
	bool BuyShopBall(int catalogIndex, int cost);
	bool RemoveShopBall(int ballIndex, int cost);
	// 登録されているレリック総数を返す。
	int GetRelicCount() const
	{
		return static_cast<int>(RelicCatalog.size());
	}
	// 指定位置のレリック定義を返す。
	const RelicDefinition* GetRelic(int index) const
	{
		return index >= 0 && index < GetRelicCount()
			? &RelicCatalog[static_cast<std::size_t>(index)]
			: nullptr;
	}
	// 指定したレリックを所持しているかを返す。
	bool HasRelic(RelicType type) const
	{
		const std::size_t index = static_cast<std::size_t>(type);
		return index < m_OwnedRelics.size() && m_OwnedRelics[index];
	}
	int GetOwnedRelicCount() const;
	ShotRelicRules MakePredictionShotRules(float launchPower) const;
	int GetRelicAttackBonus() const;
	int GetRelicDefenseBonus() const;
	// 現在ショットの衝突攻撃ボーナスを返す。
	int GetCurrentShotCollisionAttackBonus() const
	{
		return m_CurrentShotCollisionAttackBonus;
	}
	// 現在ショットのプレイヤー対敵衝突数を返す。
	int GetCurrentShotPlayerEnemyCollisionCount() const
	{
		return m_CurrentShotPlayerEnemyCollisionCount;
	}
	// 現在ショットの敵同士の衝突数を返す。
	int GetCurrentShotEnemyEnemyCollisionCount() const
	{
		return m_CurrentShotEnemyEnemyCollisionCount;
	}
	// 現在ショットの全ボール衝突数を返す。
	int GetCurrentShotBallCollisionCount() const
	{
		return m_CurrentShotPlayerEnemyCollisionCount +
			m_CurrentShotEnemyEnemyCollisionCount;
	}
	// バンクショット効果をまだ消費可能かを返す。
	bool IsCurrentShotBankShotReady() const
	{
		return m_CurrentShotBankShotReady &&
			!m_CurrentShotBankShotConsumed;
	}
	int GetEffectivePlayerBallAttack(const PlayerBallData* ball) const;
	int GetEffectivePlayerBallDefense(const PlayerBallData* ball) const;
	// ボール報酬として提示する候補数を返す。
	int GetBallOfferSize() const
	{
		return HasRelic(RelicType::ExpandedBallOffer) ? 4 : 3;
	}
	void RollShopRelicOffers();
	// ショップに提示中のレリック数を返す。
	int GetShopRelicOfferCount() const
	{
		return static_cast<int>(m_ShopRelicOffers.size());
	}
	// ショップ提示位置に対応するレリック番号を返す。
	int GetShopRelicOfferCatalogIndex(int offerIndex) const
	{
		return offerIndex >= 0 && offerIndex < GetShopRelicOfferCount()
			? m_ShopRelicOffers[static_cast<std::size_t>(offerIndex)]
			: -1;
	}
	// ショップ提示位置に対応するレリック定義を返す。
	const RelicDefinition* GetShopRelicOffer(int offerIndex) const
	{
		return GetRelic(GetShopRelicOfferCatalogIndex(offerIndex));
	}
	bool IsShopRelicOffered(int relicIndex) const;
	bool BuyShopRelicOffer(int offerIndex);
	bool BuyShopRelic(int relicIndex);
	void RollMidBossRelicOffers();
	// 中ボス報酬として提示中のレリック数を返す。
	int GetMidBossRelicOfferCount() const
	{
		return static_cast<int>(m_MidBossRelicOffers.size());
	}
	// 中ボス提示位置に対応するレリック番号を返す。
	int GetMidBossRelicOfferCatalogIndex(int offerIndex) const
	{
		return offerIndex >= 0 && offerIndex < GetMidBossRelicOfferCount()
			? m_MidBossRelicOffers[static_cast<std::size_t>(offerIndex)]
			: -1;
	}
	// 中ボス提示位置に対応するレリック定義を返す。
	const RelicDefinition* GetMidBossRelicOffer(int offerIndex) const
	{
		return GetRelic(GetMidBossRelicOfferCatalogIndex(offerIndex));
	}
	// 中ボスのレリック選択中かを返す。
	bool IsMidBossRelicSelectionActive() const
	{
		return m_IsMidBossRelicSelectionActive;
	}
	bool AcquireMidBossRelicOffer(int offerIndex);
	bool AcquireMidBossRelic(int relicIndex);
	bool BuyRelic(int relicIndex);
};
