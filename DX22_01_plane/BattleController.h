#pragma once

#include "BattleTypes.h"
#include "EnemyActionPlanner.h"
#include "GameTypes.h"
#include "ShotRelicRules.h"
#include "StageData.h"

#include <deque>
#include <cstdint>
#include <functional>
#include <random>
#include <string>

class GameWorld;
class PlayerBall;
class EnemyBall;

struct BattlePocketRules
{
    float playerDamageRatio = 0.04f;
    bool enemyInstantDefeat = true;
    float playerReturnHalfWidth = 12.0f;
    float playerReturnHalfDepth = 8.0f;
};

// BattleController自身が所有しないGame側処理だけを受け取る。
// BattleControllerからGame全体へ直接アクセスしないための境界。
struct BattleControllerHooks
{
    // ショット終了時
    std::function<void()> restorePocketedPlayer;
    std::function<void(PlayerBall*)> applyEndOfShotEffects;
    std::function<void(int playerHp, int aliveEnemies, int defeatedEnemies)>
        endShotLog;

    // Attack launch and impact are separated; HP/shield resolve only on impact.
    std::function<void(const EnemyBall&, const PlayerBall&)> enemyAttackLaunched;
    std::function<int()> playerShield;
    std::function<void(int hpDamage, int shieldAbsorbed)> enemyAttackImpact;
    // 敵攻撃時
    std::function<int()> beginEnemyAttackForecast;
    std::function<void(int predictedDamage, int actualDamage)>
        endEnemyAttackForecast;
    std::function<void(
        const std::string& source,
        int damage,
        const std::string& sourceId,
        int hpBefore,
        int hpAfter)> notifyPlayerDamage;
    std::function<void(PlayerBall*)> capturePlayerStatus;
    std::function<void(
        const std::vector<EnemySpawnData>&, int, unsigned int, int)>
        spawnPhaseEnemies;
    std::function<void(int,bool)> spawnBossMinions;

    // 敵行動計画とCharge妨害の観測結果。ログ・表示はHook先の責務。
    std::function<void(const EnemyActionTurnPlan&)>
        enemyActionPlanConfirmed;
    std::function<void(
        const EnemyBall& enemy,
        const ChargeInterruptionResult& result)>
        chargeInterrupted;

    // ターン終了時
    std::function<bool()> prepareNextTurn;

    // 想定外のタイミングで敵全滅を検出した場合のログ
    std::function<void(
        const char* source,
        BattleState previousState)> onClearStateRecovered;
};

// 戦闘中の状態遷移・ターン進行・勝敗判定を一元管理
class BattleController final
{
public:
    /// <summary>
    /// 戦闘コントローラーをメンバーの既定値で初期化する。
    /// </summary>
    BattleController() = default;

    void Initialize(
        GameWorld& world,
        BattleControllerHooks hooks);

    void StartBattle();
    void Reset();

    // 通常フレーム側の戦闘進行。
    void Update(double elapsedSeconds = 1.0 / 60.0);

    // 1 FixedStep終了後にGameから呼ぶ。
    // trueを返した場合は、そのフレームの物理catch-upを終了
    bool OnFixedStepCompleted();

    // PlayerBall等から受け取る「起きた事実」の通知。
    void BeginAimingDirection();
    void BeginAimingPower();
    void NotifyShotFired(bool heavyShot);
    void NotifyPlayerDefeated();
    // EnemyBallの一度限りのDefeat通知を、削除後も残る戦闘集計へ反映。
    void NotifyEnemyDefeated();
    void NotifyEnemyPocketDefeated();

    /// <summary>
    /// 現在の戦闘進行状態を返す。
    /// </summary>
    BattleState GetState() const
    {
        return m_State;
    }

    /// <summary>
    /// 保持している戦闘結果を返す。
    /// </summary>
    BattleResult GetResult() const
    {
        return m_Result;
    }

    /// <summary>
    /// 戦闘が活動中かを返す。
    /// </summary>
    bool IsActive() const
    {
        return m_Active;
    }

    /// <summary>
    /// 戦闘の進行状態が終了かを判定する。
    /// </summary>
    bool IsFinished() const
    {
        return m_State == BattleState::Finished;
    }

    /// <summary>
    /// 戦闘の進行状態がボール移動中かを判定する。
    /// </summary>
    bool IsBallsMoving() const
    {
        return m_State == BattleState::BallsMoving;
    }

    /// <summary>
    /// 全ボールが停止判定を満たし続けた固定ステップ数を返す。
    /// </summary>
    int GetStoppedTickCount() const
    {
        return m_AllBallsStoppedTickCount;
    }

    /// <summary>
    /// 現在の戦闘ターン番号を返す。
    /// </summary>
    int GetTurnNumber() const { return m_TurnNumber; }
    /// <summary>
    /// 戦闘開始からの固定物理ステップ数を返す。
    /// </summary>
    std::uint64_t GetFixedStepCount() const { return m_FixedStepCount; }
    /// <summary>
    /// 現在のショットで進めた固定物理ステップ数を返す。
    /// </summary>
    std::uint64_t GetShotFixedStepCount() const { return m_ShotFixedStepCount; }
    /// <summary>
    /// 戦闘状態の遷移履歴を読み取り専用の参照で返す。
    /// </summary>
    const std::deque<BattleState>& GetStateHistory() const
    {
        return m_StateHistory;
    }

    // Gameが結果を1回だけ処理するために取得
    BattleResult ConsumeResult();

    bool AreAllBallsStopped() const;
    bool AreAllEnemiesDefeated() const;

    void ResetShotState(const ShotRelicRules& rules);
    /// <summary>
    /// 現在のショットのレリック効果を読み取り専用で返す。
    /// </summary>
    const ShotRelicRules& GetShotRelicRules() const { return m_ShotRelicRules; }
    /// <summary>
    /// 現在のショットのレリック効果を設定する。
    /// </summary>
    void SetShotRelicRules(const ShotRelicRules& rules) { m_ShotRelicRules = rules; }
    bool ClaimBountyReward();

    void BeginStage(const StageData& stage);
    /// <summary>
    /// 現在のステージ種別を返す。
    /// </summary>
    StageType GetStageType() const { return m_StageType; }
    /// <summary>
    /// 現在の戦闘方式を返す。
    /// </summary>
    CombatMode GetCombatMode() const { return m_CombatMode; }
    /// <summary>
    /// 現在の戦闘方式がフェーズ制かを判定する。
    /// </summary>
    bool IsPhasePrototype() const
    {
        return m_CombatMode == CombatMode::PhasePrototype;
    }
    /// <summary>
    /// フェーズ制で、ボス戦以外の敵攻撃タイミングを使うかを判定する。
    /// </summary>
    bool UsesPhaseAttackTiming() const
    {
        return IsPhasePrototype() && m_StageType != StageType::Boss;
    }
    /// <summary>
    /// 進行中のフェーズ番号を返す。
    /// </summary>
    int GetCurrentPhase() const { return m_CurrentPhase; }
    /// <summary>
    /// ステージに設定された総フェーズ数を返す。
    /// </summary>
    int GetTotalPhases() const { return m_TotalPhases; }
    /// <summary>
    /// 次のフェーズまでの残りターン数を返す。
    /// </summary>
    int GetTurnsUntilNextPhase() const { return m_TurnsUntilNextPhase; }
    int GetNextPhaseEnemyCount() const;
    int GetLivingEnemyCount() const;
    int GetDefeatedEnemyCount() const;
    /// <summary>
    /// ポケットで撃破した敵の累計を返す。
    /// </summary>
    int GetPocketDefeatedEnemyCount() const
    {
        return m_PocketDefeatedEnemyCount;
    }
    /// <summary>
    /// ポケットの戦闘ルールを変更できる参照を返す。
    /// </summary>
    BattlePocketRules& PocketRules() { return m_PocketRules; }
    /// <summary>
    /// ポケットの戦闘ルールを読み取り専用の参照で返す。
    /// </summary>
    const BattlePocketRules& PocketRules() const { return m_PocketRules; }
    /// <summary>
    /// ポケット効果に使う乱数生成器を変更できる参照を返す。
    /// </summary>
    std::mt19937& PocketRandomEngine() { return m_PocketRandomEngine; }
    /// <summary>
    /// ポケット効果に使う乱数生成器を読み取り専用の参照で返す。
    /// </summary>
    const std::mt19937& PocketRandomEngine() const { return m_PocketRandomEngine; }
    /// <summary>
    /// 敵行動プランナーが使っている設定を返す。
    /// </summary>
    const EnemyActionConfig& GetEnemyActionConfig() const
    {
        return m_EnemyActionPlanner.Config();
    }
	void SetEnemyActionMaximumBudget(int maximumBudget);

private:
    GameWorld* m_World = nullptr;
    BattleControllerHooks m_Hooks{};

    BattleState m_State = BattleState::Inactive;
    BattleResult m_Result = BattleResult::None;
    bool m_Active = false;

    int m_AllBallsStoppedTickCount = 0;
    int m_TurnNumber = 0;
    std::uint64_t m_FixedStepCount = 0;
    std::uint64_t m_ShotFixedStepCount = 0;
    std::deque<BattleState> m_StateHistory;
    ShotRelicRules m_ShotRelicRules{};
    bool m_BountyRewardClaimed = false;
    StageType m_StageType = StageType::Normal;
    CombatMode m_CombatMode = CombatMode::Legacy;
    PhaseBattleData m_PhaseBattle{};
    int m_CurrentPhase = 0;
    int m_TotalPhases = 0;
    int m_TurnsUntilNextPhase = 0;
    std::size_t m_EnemyAttackCursor = 0;
    bool m_EnemyAttackStarted = false;
    bool m_EnemyAttackLaunched = false;
    double m_EnemyAttackDelay = 0.0;
    int m_EnemyAttackPredicted = 0;
    int m_EnemyAttackHpBefore = 0;
    int m_DefeatedEnemyCount = 0;
    int m_PocketDefeatedEnemyCount = 0;
    BattlePocketRules m_PocketRules{};
    std::mt19937 m_PocketRandomEngine{ std::random_device{}() };
    EnemyActionPlanner m_EnemyActionPlanner;
	EnemyActionConfig m_DefaultEnemyActionConfig{};

private:
    friend class BattleControllerTestAccess;

    void CompleteShot();
    void ProcessEnemyAttack();
    void ProcessTurnEnd();
    void PlanNextTurn();
    void PlanPhaseTurn();
    bool TryAdvancePhase();
    bool TryAdvancePhase(int livingEnemyCount);
    bool IsPhaseVictoryForLivingCount(int livingEnemyCount) const;
    void ResolveChargeInterruptions();

    void SetState(BattleState state, const char* reason);
    void LogProgressDiagnostic(const char* reason) const;

    bool TryRecoverClearedBattle(const char* source);

    void FinishBattle(BattleResult result);
};
