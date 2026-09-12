#pragma once
#include "ShotRelicRules.h"
#include "BossShotPlanner.h"
#include "GameDebugController.h"
#include "GameWorld.h"
#include "BattleController.h"
#include "BalanceAutoPlayer.h"
#include "BalanceValidationController.h"
#include "DynamicBalanceController.h"
#include "RunController.h"
#include "SceneManager.h"
#include "RunProgressController.h"
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
#include "CushionChargeRules.h"
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
	CushionChargeRules::State m_CushionCharges{};
	bool m_CushionBoostConsumedThisShot = false;
	int m_PlayerShield = 0;

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
	RunController m_RunController;
	std::optional<StageData> m_McpNextStageOverride;
	std::optional<StageData> m_McpCurrentStageOverride;

	BossShotPlanner m_BossShotPlanner;
	int m_SelectedOfferIndex = 0;
	int m_SelectedHoldIndex = -1;

	// ==========================
	// 報酬・ショップUI用
	// ==========================
	int m_SelectedRewardIndex = 0;
	int m_SelectedRewardBallIndex = 0;

	std::string m_RewardMessage;           // 購入結果などの表示
	bool m_IsClearRewardChosen = false;
	bool m_ClearRewardMouseConfirmed = false;
	bool m_IsMidBossRelicSelectionActive = false;
	int m_SelectedRelicOfferIndex = 0;
	static constexpr int kNormalRouteAreaGoal =
		RunProgressController::NormalRouteAreaGoal;

	BalanceAutoPlayer m_BalanceAutoPlayer;
	std::unique_ptr<GameMcpBridge> m_GameMcpBridge;
	std::unique_ptr<GamePresentation> m_GamePresentation;
	nlohmann::json m_PendingShotTelemetry = nlohmann::json::object();
	std::uint32_t m_RunRandomSeed = 0;
	std::uint32_t m_StageSelectionSeed = 0;
	std::uint32_t m_RouteSelectionSeed = 0;
	std::uint32_t m_RouteSelectionCounter = 0;
	BalanceValidationController m_BalanceValidationController;

	// 基準難易度はラン中に固定し、DDAは独立した救済機能として扱う。
	std::string m_BaselineDifficultyProfile = "normal";
	float m_BaselineEnemyHpMultiplier = 1.0f;
	int m_BaselineEnemyAttackDelta = 0;

	// エンカウントの脅威度コストは、単純な敵数とは分けてログへ記録する。
	std::unordered_map<std::string, float> m_EnemyThreatCosts;
	std::unordered_map<std::string, float> m_StageThreatTargets;
	float m_StageDataLayoutThreatMultiplier = 1.0f;
	float m_DenseLayoutThreatMultiplier = 1.25f;
	float m_McpLayoutThreatMultiplier = 1.0f;

	DynamicBalanceController m_DynamicBalanceController;
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
	GameDebugController m_DebugController;
	void DrawDebugMode();
	bool UpdateDebugMode();
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

	friend class GameMcpBridge;
	friend class GameSaveManager;
	friend class BalanceAutoPlayer;
	friend class GameDebugController;
	friend class GamePresentation;
	friend class BossShotPlanner;

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
	void LoadDifficultyProfileConfig(
		const std::string& filePath =
			"assets/data/difficulty_profiles.json");
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
	StageType GetScheduledStageType() const;
	void ApplyBalanceAutoBallSelection(int offerIndex);
	void MarkBalanceAutoRewardChosen(
		int rewardIndex,
		int rewardBallIndex,
		const std::string& message);
	int GetClearRewardUpgradeCost(int ballIndex) const;
	bool ApplyClearRewardUpgrade(int ballIndex, int& chargedCost);
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
	bool IsDebugMode() const { return m_DebugController.IsActive(); }
	void ApplyDebugBattlePlayer(PlayerBall* player);
	void ApplyDebugBattleEnemy(EnemyBall* enemy, std::size_t index);
	const std::vector<DirectX::SimpleMath::Vector3>& GetDebugBreakBallPositions() const
	{
		return m_DebugController.GetActiveBreakBallPositions();
	}
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
		return m_BalanceAutoPlayer.IsEnabled();
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
	void NotifyPlayerWallCollision(
		int cushionRegion,
		DirectX::SimpleMath::Vector3& reflectedVelocity);
	void NotifyPlayerChainImpact(
		EnemyBall* directTarget,
		int attackDamage,
		float radius);
	void NotifyPlayerChainImpact(
		const DirectX::SimpleMath::Vector3& center,
		const EnemyBall* excludedTarget,
		int attackDamage,
		float radius);
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
	void ApplyDynamicBalanceToEnemyData(EnemyData& enemyData) const;
	DynamicBalanceController& DynamicBalance()
	{
		return m_DynamicBalanceController;
	}
	const DynamicBalanceController& DynamicBalance() const
	{
		return m_DynamicBalanceController;
	}
	BalanceValidationController& BalanceValidation()
	{
		return m_BalanceValidationController;
	}
	const BalanceValidationController& BalanceValidation() const
	{
		return m_BalanceValidationController;
	}
	void NotifyBalanceAutoFullHpEnemySurvived();
	// 次回のルート選択に使う一意な乱数シードを返す。
	std::uint32_t GetNextRouteRandomSeed()
	{
		return m_RouteSelectionSeed + m_RouteSelectionCounter++;
	}
	// 山札に残っているボール数を返す。
	int GetPlayerDeckCount() const { return m_RunController.Deck().GetDrawPileCount(); }
	// 捨て札にあるボール数を返す。
	int GetPlayerDiscardCount() const { return m_RunController.Deck().GetDiscardPileCount(); }

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
		return m_RunController.Status().money;
	}
	// プレイヤーの現在HPを返す。
	int GetPlayerCurrentHp() const { return m_RunController.Status().currentHp; }
	// プレイヤーの最大HPを返す。
	int GetPlayerMaxHp() const { return m_RunController.Status().maxHp; }
	int GetRestHealAmount() const;
	int GetRestHealPercent() const;
	// 休憩による回復を利用できるかを返す。
	bool CanRestHeal() const
	{
		return m_RunController.CanRestHeal();
	}
	// 現在までにクリアした戦闘数を返す。
	int GetClearedStageCount() const
	{
		return m_RunController.Progress().GetClearedBattleCount();
	}
	// 通常ルートの現在進行数を返す。
	int GetAreaProgress() const { return m_RunController.Progress().GetAreaProgress(); }
	// 現在のランマップを読み取り専用で返す。
	const RunMap& GetRunMap() const { return m_RunController.Progress().GetMap(); }
	// 選択可能なマップノードを選ぶ。
	bool ChooseMapNode(int nodeId) { return m_RunController.Progress().ChooseNode(nodeId); }
	// 通常ルートの完了目標エリア数を返す。
	int GetNormalRouteAreaGoal() const { return kNormalRouteAreaGoal; }
	// 現在のランフェーズを返す。
	RunPhase GetRunPhase() const { return m_RunController.Progress().GetPhase(); }
	// 最終ボス前の準備フェーズかを返す。
	bool IsBossPreparation() const
	{
		return m_RunController.Progress().IsBossPreparation();
	}
	// 最終ボスを選択可能なフェーズかを返す。
	bool IsFinalBossRoute() const
	{
		return m_RunController.Progress().IsFinalBossReady();
	}
	// プレイヤー表示用の現在進行値を返す。
	int GetPlayerProgress() const { return m_RunController.Status().progress; }
	// 現在選択されているステージIDを返す。
	const std::string& GetSelectedStageId() const
	{
		return m_RunController.Status().GetSelectedStageId();
	}

	// 報酬対象となるデッキ内ボール数を返す。
	int GetDeckBallCount() const { return m_RunController.Deck().GetRewardTargetCount(); }
	// デッキの最小構成数を返す。
	int GetMinimumDeckSize() const { return PlayerDeck::MinimumDeckSize; }
	// 指定位置のデッキ内ボールを返す。
	const PlayerBallData* GetDeckBall(int index) const { return m_RunController.Deck().GetRewardTarget(index); }
	const CushionChargeRules::State& GetCushionCharges() const { return m_CushionCharges; }
	int GetChargedCushionCount() const { return CushionChargeRules::ActiveCount(m_CushionCharges); }
	bool WasCushionBoostConsumedThisShot() const { return m_CushionBoostConsumedThisShot; }
	int GetPlayerShield() const { return m_PlayerShield; }
	int AbsorbPlayerShieldDamage(int damage);
	// ショップの商品候補となるボール数を返す。
	int GetShopBallCount() const { return m_RunController.Deck().GetCatalogCount(); }
	// 指定位置のショップ用ボール情報を返す。
	const PlayerBallData* GetShopBall(int index) const { return m_RunController.Deck().GetCatalogBall(index); }
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
		return m_RunController.HasRelic(type);
	}
	int GetOwnedRelicCount() const;
	ShotRelicRules MakePredictionShotRules(float launchPower) const;
	int GetRelicAttackBonus() const;
	int GetRelicDefenseBonus() const;
	// 現在ショットの衝突攻撃ボーナスを返す。
	int GetCurrentShotCollisionAttackBonus() const
	{
		return m_BattleController.GetShotRelicRules().collisionBonus;
	}
	// 現在ショットのプレイヤー対敵衝突数を返す。
	int GetCurrentShotPlayerEnemyCollisionCount() const
	{
		return m_BattleController.GetShotRelicRules().playerEnemyContacts;
	}
	// 現在ショットの敵同士の衝突数を返す。
	int GetCurrentShotEnemyEnemyCollisionCount() const
	{
		return m_BattleController.GetShotRelicRules().enemyEnemyContacts;
	}
	// 現在ショットの全ボール衝突数を返す。
	int GetCurrentShotBallCollisionCount() const
	{
		const auto& rules = m_BattleController.GetShotRelicRules();
		return rules.playerEnemyContacts + rules.enemyEnemyContacts;
	}
	// バンクショット効果をまだ消費可能かを返す。
	bool IsCurrentShotBankShotReady() const
	{
		const auto& rules = m_BattleController.GetShotRelicRules();
		return rules.bankReady && !rules.bankConsumed;
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
		return static_cast<int>(m_RunController.ShopRelicOffers().size());
	}
	// ショップ提示位置に対応するレリック番号を返す。
	int GetShopRelicOfferCatalogIndex(int offerIndex) const
	{
		return offerIndex >= 0 && offerIndex < GetShopRelicOfferCount()
			? m_RunController.ShopRelicOffers()[static_cast<std::size_t>(offerIndex)]
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
		return static_cast<int>(m_RunController.MidBossRelicOffers().size());
	}
	// 中ボス提示位置に対応するレリック番号を返す。
	int GetMidBossRelicOfferCatalogIndex(int offerIndex) const
	{
		return offerIndex >= 0 && offerIndex < GetMidBossRelicOfferCount()
			? m_RunController.MidBossRelicOffers()[static_cast<std::size_t>(offerIndex)]
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
