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
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

//オブジェクト情報のあるファイルをインクルード
//#include "
// .h"
//#include "Ground.h"


#include "Camera.h"
#include "BallStatus.h"
#include "CushionChargeRules.h"
#include "HeavyCollisionRules.h"
#include "PierceTraceRules.h"
#include "AnchorStackRules.h"
#include "GameTypes.h"
#include "GameEvent.h"
#include "RunResultSnapshot.h"
#include "RunStatisticsTracker.h"
#include "SettingsManager.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "GameObject.h"
#include "FixedStepClock.h"
#include "json/json.hpp"

class EnemyBall;
class BallComponent;
struct EnemyData;
class GameMcpBridge;
class GamePresentation;
class GameSaveManager;
class PlayerBall;
class Scene;

class Game
{
private:
	// ===== ゲーム全体の基盤 =====
	// 唯一のGameインスタンスと、シーン・カメラ・ゲーム空間を管理する。
	static Game* m_Instance;//ゲームインスタンス

	SceneManager m_SceneManager;

	// カメラ
	Camera&  m_Camera = Camera::GetInstance();

	GameWorld m_World;

	// ===== 戦闘状態とショット中の効果 =====
	// 戦闘の進行、クッション蓄積、シールド、直近の戦闘結果を保持する。
	BattleController m_BattleController;
	CushionChargeRules::State m_CushionCharges{};
	bool m_CushionBoostConsumedThisShot = false;
	int m_CushionStrongUsesThisShot = 0;
	int m_SynergyDamageBonusThisShot = 0;
	HeavyCollisionRules::State m_HeavyCollisions{};
	PierceTraceRules::State m_PierceTraces{};
	PierceTraceRules::ShotUseState m_PierceTraceUse{};
	AnchorStackRules::State m_AnchorStacks{};
	DirectX::SimpleMath::Vector3 m_TraceSegmentStart = DirectX::SimpleMath::Vector3::Zero;
	bool m_TraceSegmentValid = false;
	bool m_TraceSegmentPierced = false;
	bool m_TraceDriverExpansionArmed = false;
	bool m_TraceDriverExpansionSegment = false;
	bool m_TracePierceBenefitActive = false;
	bool m_HeavyFinisherConsumedThisShot = false;
	bool m_AnchorFinisherTriggeredThisShot = false;
	EnemyBall* m_AnchorContactTarget = nullptr;
	std::unordered_set<const EnemyBall*> m_PiercedEnemiesThisShot;
	int m_PlayerShield = 0;

	// 直近に確定した戦闘結果。
	// MCPやResult側から参照できるようGameが履歴だけ保持する。
	BattleResult m_LastBattleResult = BattleResult::None;

	// ClearRewardは戦闘内部状態ではないためGame側で保持する。
	static constexpr int kClearRewardBallOfferSize = 3;
	bool m_IsClearRewardActive = false;

	// ===== 固定ステップ物理更新 =====
	// 表示フレームレートに依存せず物理演算を進め、実行回数や上限到達を計測する。
	FixedStepClock m_PhysicsClock;
	bool m_ResetPhysicsElapsed = true;
	std::uint64_t m_PhysicsTickCount = 0;
	int m_PhysicsStepsLastFrame = 0;
	int m_PhysicsSubstepsLastTick = 0;
	std::uint64_t m_PhysicsSubstepLimitCount = 0;
	void UpdateFixedPhysics(double elapsedSeconds);
	void InitializeBattleController();

	// ===== プレイヤー状態とラン進行 =====
	// 初期ステータス、デッキ・所持金・マップ進行、MCPからのステージ差し替えを管理する。
	BallStatus m_DefaultPlayerStatus{};
	PlayerRunStatus m_DefaultPlayerRunStatus{};
	RunController m_RunController;
	std::optional<StageData> m_McpNextStageOverride;
	std::optional<StageData> m_McpCurrentStageOverride;

	// ===== ボスAIとボール選択UI =====
	// ボスのショット計画と、手球候補の選択・ホールド位置を保持する。
	BossShotPlanner m_BossShotPlanner;
	int m_SelectedOfferIndex = 0;
	int m_SelectedHoldIndex = -1;

	// ===== 報酬・ショップUI =====
	// 報酬種別、対象ボール、レリック候補の選択状態と結果メッセージを保持する。
	int m_SelectedRewardIndex = 0;
	int m_SelectedRewardBallIndex = 0;
	std::vector<int> m_ClearRewardBallOfferCatalogIndices;

	std::string m_RewardMessage;           // 購入結果などの表示
	bool m_IsClearRewardChosen = false;
	bool m_ClearRewardMouseConfirmed = false;
	bool m_IsMidBossRelicSelectionActive = false;
	int m_SelectedRelicOfferIndex = 0;
	static constexpr int kNormalRouteAreaGoal =
		RunProgressController::NormalRouteAreaGoal;

	// ===== 自動検証・MCP連携・再現用テレメトリ =====
	// 自動プレイ、外部AI連携、画面表示、ショット記録、再現可能な乱数シードを管理する。
	BalanceAutoPlayer m_BalanceAutoPlayer;
	std::unique_ptr<GameMcpBridge> m_GameMcpBridge;
	std::unique_ptr<GamePresentation> m_GamePresentation;
	nlohmann::json m_PendingShotTelemetry = nlohmann::json::object();
	std::uint32_t m_RunRandomSeed = 0;
	std::uint32_t m_StageSelectionSeed = 0;
	std::uint32_t m_RouteSelectionSeed = 0;
	std::uint32_t m_RouteSelectionCounter = 0;
	BalanceValidationController m_BalanceValidationController;

	// ===== 難易度とエンカウントバランス =====
	// ラン開始時の基準難易度と、敵編成・配置ごとの脅威度計算条件を保持する。
	// 基準難易度はラン中に固定し、進行度に応じた敵補正を適用する。
	std::string m_BaselineDifficultyProfile = "normal";
	float m_BaselineEnemyHpMultiplier = 1.0f;
	int m_BaselineEnemyAttackDelta = 0;

	// エンカウントの脅威度コストは、単純な敵数とは分けてログへ記録する。
	std::unordered_map<std::string, float> m_EnemyThreatCosts;
	std::unordered_map<std::string, float> m_StageThreatTargets;
	float m_StageDataLayoutThreatMultiplier = 1.0f;
	float m_DenseLayoutThreatMultiplier = 1.25f;
	float m_McpLayoutThreatMultiplier = 1.0f;

	// ===== 固定難易度・セーブ・永続進行 =====
	// 進行度補正、ラン復元、統計、解放状態、アセンション、ゲーム設定を管理する。
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

	// ===== デバッグモード =====
	// デバッグ戦闘の起動・表示・終了と、検証用設定の保存・復元を行う。
	GameDebugController m_DebugController;
	void DrawDebugMode();
	bool UpdateDebugMode();
	void EndDebugMode();
	void FinishDebugBattle(bool victory);
	void ApplyDebugRunSettings();
	bool LoadDebugPreset();

	// ===== ポーズ・入力要求・ラン結果 =====
	// UIから受けた操作要求を次回更新で処理し、直近の確定済みラン結果を保持する。
	bool m_IsPaused = false;
	bool m_MousePauseToggle = false;
	bool m_MouseSaveRequested = false;
	bool m_MouseFullscreenToggle = false;
	bool m_PauseConfirmTitle = false;
	bool m_PendingDisplayApply = false;
	RunResultSnapshot m_LastRunResult{};

	// ===== 密接に連携するサブシステム =====
	// Gameの内部状態を直接読み書きする管理クラスに限定してアクセスを許可する。
	friend class GameMcpBridge;
	friend class GameSaveManager;
	friend class BalanceAutoPlayer;
	friend class GameDebugController;
	friend class GamePresentation;
	friend class BossShotPlanner;

	// ===== 戦闘終了とクリア報酬 =====
	// 敗北終了、勝利報酬の開始・更新・描画、ボール選択とプレビューを行う。
	void ProcessGameOver();
	void StartClearReward();
	void RollClearRewardBallOffers();
	void UpdateClearReward();
	void DrawClearRewardUI();
	bool BeginBallSelection();
	void UpdateBallSelection();
	void DrawBallSelectionUI();
	void ApplySelectedBallPreview();

	// ===== プレイヤーボール・デッキ・レリック適用 =====
	// ラン中ステータスの反映、レリック候補の抽選と付与、ショット効果の開始・終了処理を行う。
	void ApplyPlayerRunStatusTo(PlayerBall* player);
	void ApplyRelicModifiersTo(PlayerBall* player);
	std::vector<int> RollRelicOffers(int count, bool midBoss);
	bool GrantRelic(int relicIndex, const char* source);
	ShotRelicRules CaptureShotRelicRules() const;
	void CommitShotRelicRules(const ShotRelicRules& rules);
	void ResetShotRelicState(PlayerBall* player = nullptr);
	void ApplyEndOfShotRelicEffects(PlayerBall* player);

	// ===== デッキ循環とステージ報酬 =====
	// 現在ボールの状態を保存し、山札・捨て札を更新して次のボールを準備し、報酬金を集計する。
	void SaveDebugSnapshot();
	void CaptureCurrentPlayerStatus();
	void DiscardCurrentPlayerBall();
	bool PrepareNextPlayerBall();
	int CalculateStageRewardMoney() const;
	void CollectStageRewardMoney();

	// ===== バランス設定の読み込み =====
	// 難易度、エンカウント脅威度、ポケットルールをJSONから構築する。
	void LoadDifficultyProfileConfig(
		const std::string& filePath =
			"assets/data/difficulty_profiles.json");
	void LoadEncounterBalanceConfig(
		const std::string& filePath =
			"assets/data/encounter_balance.json");
	void LoadPocketRulesConfig(
		const std::string& filePath =
			"assets/data/pocket_rules.json");

	// ===== ポケット後のボール復帰 =====
	// ポケットに落ちた敵・プレイヤーを安全な位置へ順番に戻す。
	void RestoreNextPocketedEnemy();
	void RestorePocketedPlayer();
	DirectX::SimpleMath::Vector3 FindEnemyPocketReturnPosition(
		const EnemyBall* returningEnemy) const;

	// ===== 自動プレイの報酬選択 =====
	// 次のステージ種別を判定し、自動検証でのボール報酬・強化選択と保留候補を整理する。
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

	// ===== 破棄オブジェクトの後処理 =====
	// 削除予定になったゲームオブジェクトをWorldから取り除く。
	void RemoveDestroyedGameObjects();

	// ===== セーブ・ポーズ・ラン結果とルート遷移 =====
	// チェックポイント保存、タイトル復帰、ラン結果確定、永続進行記録、次ルートへの遷移を行う。
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

	// ===== イベント配信とデバッグ戦闘情報 =====
	// 戦闘イベントを各記録系へ通知し、予測値とプレイヤー被弾履歴を更新する。
	void PublishGameEvent(const GameEvent& event);
	void RecordDebugPlayerDamage(
		const std::string& source,
		const std::string& sourceId,
		int damage,
		int hpBefore,
		int hpAfter);

public:
	// ===== デバッグ機能の公開操作 =====
	// デバッグモードの起動・状態取得と、検証用のプレイヤー・敵・ブレイクボール設定を反映する。
	void OpenDebugMode();
	// デバッグモードが有効かを返す。
	bool IsDebugMode() const { return m_DebugController.IsActive(); }
	void ApplyDebugBattlePlayer(PlayerBall* player);
	void ApplyDebugBattleEnemy(EnemyBall* enemy, std::size_t index);
	const std::vector<DirectX::SimpleMath::Vector3>& GetDebugBreakBallPositions() const
	{
		return m_DebugController.GetActiveBreakBallPositions();
	}

	// ===== ライフサイクル =====
	// Gameの生成・破棄と、初期化、フレーム更新、描画、終了処理を行う。
	Game(); // コンストラクタ
	~Game(); // デストラクタ

	static void Init(); // 初期化
	static void Update(double elapsedSeconds); // 入力・表示フレームの更新
	static void ResetFrameTiming();
	static void Draw(); // 描画
	static void Uninit(); // 終了処理

	// ===== ゲーム空間とシーン管理 =====
	// Game本体の取得、オブジェクトの生成・削除、シーン切り替えを行う。
	static Game* GetInstance();
	GameObject* CreateGameObject(const std::string& name);

	void ChangeScene(SceneType sceneType);	//シーンを変更
	// 現在のシーンを読み取り用に返す。
	Scene* GetCurrentScene() const { return m_SceneManager.Get(); }
	// 現在のシーン種別を返す。
	SceneType GetCurrentSceneType() const { return m_SceneManager.GetType(); }
	void DeleteGameObject(GameObject* gameObject);
	void DeleteAllGameObjects();

	// ===== カメラとポーズ中の入力要求 =====
	// カメラを公開し、ポーズ・セーブ・全画面切り替えを次回更新へ予約する。
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

	// ===== 戦闘状態の参照 =====
	// 現在の戦闘フェーズ・結果・稼働状態と、報酬UIやボール停止状態を返す。
	// BattleStateを取得する
	BattleState GetBattleState() const
	{
		return m_BattleController.GetState();
	}

	BattleResult GetLastBattleResult() const
	{
		return m_LastBattleResult;
	}

	bool IsClearRewardActive() const
	{
		return m_IsClearRewardActive;
	}
	int GetClearRewardBallOfferCount() const
	{
		return static_cast<int>(m_ClearRewardBallOfferCatalogIndices.size());
	}
	int GetClearRewardBallOfferCatalogIndex(int offerIndex) const
	{
		return offerIndex >= 0 && offerIndex < GetClearRewardBallOfferCount()
			? m_ClearRewardBallOfferCatalogIndices[static_cast<std::size_t>(offerIndex)]
			: -1;
	}
	const PlayerBallData* GetClearRewardBallOffer(int offerIndex) const
	{
		return m_RunController.Deck().GetCatalogBall(
			GetClearRewardBallOfferCatalogIndex(offerIndex));
	}
	bool AddClearRewardBallOffer(int offerIndex)
	{
		return m_RunController.Deck().AddCatalogBall(
			GetClearRewardBallOfferCatalogIndex(offerIndex));
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

	// ===== オブジェクト所属確認 =====
	// 指定オブジェクトまたはコンポーネントが現在のWorldに存在するかを返す。
	bool ContainsGameObject(const GameObject* gameObject) const;
	bool ContainsComponent(const Component* component) const;

	// ===== ランの初期化・セーブ・統計 =====
	// プレイヤー初期データの読み込み、新規ラン開始、セーブ復元、ラン統計の参照を行う。
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
	// ===== ボスショットAI =====
	// 現在盤面の候補ショットを評価し、指定された計画を検証済み状態で実行する。
    nlohmann::json EvaluateBossShots();
    bool FireBossPlannedShot(const std::string& candidateId, const std::string& stateKey);

	// ===== 永続進行とアセンション =====
	// 直近のラン結果、解放済み要素、選択中のアセンションを参照・更新する。
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
	// ===== ゲーム設定と演出許可 =====
	// 現在の設定と、振動・画面フラッシュ・カメラ揺れの有効状態を返す。
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

	// ===== プレイヤー状態とステージ遷移 =====
	// ボールとランのステータスを同期し、戦闘・ショップ・休憩・報酬間の遷移を進める。
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

	// ===== ショットと戦闘イベント通知 =====
	// 照準・威力決定・発射・衝突・撃破を戦闘ルール、レリック、統計、演出へ通知する。
	void OnPlayerShotFired(PlayerBall* player);
	void NotifyBattleAimDirectionStarted();
	void NotifyBattlePowerSelectionStarted();
	void NotifyBattleShotCancelled();
	void NotifyBattlePlayerDefeated();	
	void NotifyPlayerWallCollision(
		int cushionRegion,
		const DirectX::SimpleMath::Vector3& playerPosition,
		DirectX::SimpleMath::Vector3& reflectedVelocity);
	void NotifyEnemyEnemySynergyCollision(
		EnemyBall* first,
		EnemyBall* second,
		const DirectX::SimpleMath::Vector3& firstVelocityBefore,
		const DirectX::SimpleMath::Vector3& secondVelocityBefore);
	int NotifyPlayerEnemySynergyCollision(EnemyBall* enemy);
	void NotifyPlayerPiercedEnemy(
		EnemyBall* enemy,
		const DirectX::SimpleMath::Vector3& playerPosition,
		bool refracted);
	void NotifyPlayerDirectionChange(
		const DirectX::SimpleMath::Vector3& playerPosition);
	void NotifyTraceMovement(
		BallComponent& ball,
		const DirectX::SimpleMath::Vector3& from,
		const DirectX::SimpleMath::Vector3& to);
	void NotifyPlayerChainImpact(
		EnemyBall* directTarget,
		int attackDamage,
		float radius);
	void NotifyPlayerChainImpact(
		const DirectX::SimpleMath::Vector3& center,
		const EnemyBall* excludedTarget,
		int attackDamage,
		float radius);
	void NotifyAnchorStopped(EnemyBall* directTarget = nullptr);
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

	// ===== ポケット判定と復帰位置 =====
	// 敵・プレイヤーの落下処理、フィニッシャー条件、復帰順と復帰位置、落下ダメージを決定する。
	void HandleEnemyPocket(EnemyBall* enemy);
	DirectX::SimpleMath::Vector3 FindPlayerPocketReturnPosition(
		const PlayerBall* player);
	float GetCurrentPocketFinisherRatio() const;
	bool IsEnemyPocketFinisherEligible(const EnemyBall* enemy) const;
	int GetPocketQueueIndex(const EnemyBall* enemy) const;
	int GetPlayerPocketDamageAmount() const;

	// ===== 難易度補正とバランス検証 =====
	// 検証イベントを記録し、敵データへ固定難易度と進行度の補正を適用する。
	void RecordBalanceEvent(
		const std::string& eventType,
		const nlohmann::json& details = nlohmann::json::object());
	void ApplyEnemyDifficultyScaling(EnemyData& enemyData) const;
	void NotifyBalanceAutoFullHpEnemySurvived();

	// ===== ルート選択用乱数とデッキ枚数 =====
	// 山札と捨て札の枚数を返す。
	// 山札に残っているボール数を返す。
	int GetPlayerDeckCount() const { return m_RunController.Deck().GetDrawPileCount(); }
	// 捨て札にあるボール数を返す。
	int GetPlayerDiscardCount() const { return m_RunController.Deck().GetDiscardPileCount(); }

	// ===== 戦闘対象とWorld内オブジェクト検索 =====
	// 敵全滅を判定し、型・保持コンポーネント・タグを条件にWorldを検索する。
	bool AreAllEnemiesDefeated() const;

	// ゲーム空間から、指定した型のコンポーネントをすべて取得する。
	template<typename T> std::vector<T*> GetComponents()
	{
		return m_World.GetComponents<T>();
	}

	// 指定したタグを持つゲームオブジェクトを取得する。
	std::vector<GameObject*> GetGameObjectsWithTag(GameObjectTag tag)
	{
		return m_World.GetObjectsWithTag(tag);
	}

	// ===== プレイヤー資源とランマップ進行 =====
	// 所持金・HP・休憩回復量を参照し、現在の進行と選択可能なマップノードを管理する。
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
	// 現在選択されているステージIDを返す。
	const std::string& GetSelectedStageId() const
	{
		return m_RunController.Status().GetSelectedStageId();
	}

	// ===== デッキ・クッション効果・シールド =====
	// 報酬対象のボールとデッキ下限を参照し、クッション蓄積とシールドによるダメージ軽減を管理する。
	// 報酬対象となるデッキ内ボール数を返す。
	int GetDeckBallCount() const { return m_RunController.Deck().GetRewardTargetCount(); }
	// デッキの最小構成数を返す。
	int GetMinimumDeckSize() const { return PlayerDeck::MinimumDeckSize; }
	// 指定位置のデッキ内ボールを返す。
	const PlayerBallData* GetDeckBall(int index) const { return m_RunController.Deck().GetRewardTarget(index); }
	// 現在のショットに割り当てられたボールを返す。
	const PlayerBallData* GetCurrentPlayerBallData() const { return m_RunController.Deck().GetCurrent(); }
	const CushionChargeRules::State& GetCushionCharges() const { return m_CushionCharges; }
	bool WasCushionBoostConsumedThisShot() const { return m_CushionBoostConsumedThisShot; }
	int GetHeavyCollisionCount() const { return m_HeavyCollisions.collisionCount; }
	const PierceTraceRules::State& GetPierceTraceState() const { return m_PierceTraces; }
	int GetPlayerAnchorStacks() const { return m_AnchorStacks.playerStacks; }
	int GetCushionStrongUsesThisShot() const { return m_CushionStrongUsesThisShot; }
	int GetPlayerShield() const { return m_PlayerShield; }
	int AbsorbPlayerShieldDamage(int damage);

	// ===== ショップと休憩所のボール操作 =====
	// ボールの候補取得、購入・削除・強化と、休憩所で実行できる回復・強化を管理する。
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

	// ===== レリック所持とステータス補正 =====
	// レリック定義と所持状態を参照し、予測用ショットルールと攻防補正値を生成する。
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

	// ===== 現在ショットのレリック実績 =====
	// 衝突ボーナス、衝突回数、バンクショット効果の残り状態と、有効な攻防値を返す。
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
	// ボール報酬として提示する候補数を返す。
	int GetBallOfferSize() const
	{
		return HasRelic(RelicType::ExpandedBallOffer) ? 4 : 3;
	}

	// ===== ショップのレリック候補 =====
	// ショップ用の候補を抽選し、表示位置から定義を取得して購入を適用する。
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

	// ===== 中ボス報酬のレリック候補 =====
	// 中ボス報酬用の候補を抽選し、表示位置から定義を取得して無料獲得を適用する。
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
	bool AcquireMidBossRelicOffer(int offerIndex);
	bool AcquireMidBossRelic(int relicIndex);
	bool BuyRelic(int relicIndex);
};
