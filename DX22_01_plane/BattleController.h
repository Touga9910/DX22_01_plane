#pragma once

#include "GameTypes.h"
#include "ShotRelicRules.h"
#include "StageData.h"

#include <deque>
#include <functional>
#include <random>
#include <string>

class GameWorld;
class PlayerBall;
class EnemyBall;

struct BattlePocketRules
{
    float playerDamageRatio = 0.04f;
    float normalFinisherRatio = 0.30f;
    float midBossFinisherRatio = 0.20f;
    float bossFinisherRatio = 0.10f;
    float playerReturnHalfWidth = 12.0f;
    float playerReturnHalfDepth = 8.0f;
    float enemyReturnX = 0.0f;
    float enemyReturnTopEdgeOffset = 10.0f;
};

// 1回の戦闘内部だけで使用する進行状態。
// ClearReward / GameOverは戦闘外の処理なので含めない。
enum class BattleState
{
    Inactive,

    AimingDirection,
    AimingPower,
    ConfirmShot,

    BallsMoving,
    EnemyAttack,
    TurnEnd,

    Finished,
};

// 1回の戦闘結果。
enum class BattleResult
{
    None,
    Victory,
    Defeat,
};

const char* ToString(BattleState state);
const char* ToString(BattleResult result);

// BattleController自身が所有しないGame側処理だけを受け取る。
// BattleControllerからGame全体へ直接アクセスしないための境界。
struct BattleControllerHooks
{
    // ショット終了時
    std::function<void()> restorePocketedPlayer;
    std::function<void(PlayerBall*)> applyEndOfShotEffects;
    std::function<void(int playerHp, int aliveEnemies, int defeatedEnemies)>
        endShotLog;
    std::function<void()> finishDynamicBalanceShot;

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
    std::function<void()> restoreNextPocketedEnemy;

    // ターン終了時
    std::function<bool()> prepareNextTurn;

    // 想定外のタイミングで敵全滅を検出した場合のログ
    std::function<void(
        const char* source,
        BattleState previousState)> onClearStateRecovered;
};

// 戦闘中の状態遷移・ターン進行・勝敗判定を一元管理する。
class BattleController final
{
public:
    BattleController() = default;

    void Initialize(
        GameWorld& world,
        BattleControllerHooks hooks);

    void StartBattle();
    void Reset();

    // 通常フレーム側の戦闘進行。
    void Update();

    // 1 FixedStep終了後にGameから呼ぶ。
    // trueを返した場合は、そのフレームの物理catch-upを終了する。
    bool OnFixedStepCompleted();

    // PlayerBall等から受け取る「起きた事実」の通知。
    void BeginAimingDirection();
    void BeginAimingPower();
    void BeginConfirmShot();
    void NotifyShotFired();
    void NotifyPlayerDefeated();

    BattleState GetState() const
    {
        return m_State;
    }

    BattleResult GetResult() const
    {
        return m_Result;
    }

    bool IsActive() const
    {
        return m_Active;
    }

    bool IsFinished() const
    {
        return m_State == BattleState::Finished;
    }

    bool IsBallsMoving() const
    {
        return m_State == BattleState::BallsMoving;
    }

    int GetStoppedTickCount() const
    {
        return m_AllBallsStoppedTickCount;
    }

    // Gameが結果を1回だけ処理するために取得する。
    BattleResult ConsumeResult();

    bool AreAllBallsStopped() const;
    bool AreAllEnemiesDefeated() const;

    void ResetShotState(const ShotRelicRules& rules);
    const ShotRelicRules& GetShotRelicRules() const { return m_ShotRelicRules; }
    void SetShotRelicRules(const ShotRelicRules& rules) { m_ShotRelicRules = rules; }
    bool ClaimBountyReward();

    void BeginStage(StageType stageType);
    StageType GetStageType() const { return m_StageType; }
    BattlePocketRules& PocketRules() { return m_PocketRules; }
    const BattlePocketRules& PocketRules() const { return m_PocketRules; }
    std::mt19937& PocketRandomEngine() { return m_PocketRandomEngine; }
    const std::mt19937& PocketRandomEngine() const { return m_PocketRandomEngine; }
    void QueuePocketedEnemy(EnemyBall* enemy);
    EnemyBall* PopPocketedEnemy();
    int GetPocketQueueIndex(const EnemyBall* enemy) const;
    std::size_t GetPocketQueueSize() const { return m_PocketedEnemyQueue.size(); }
    void ClearPocketQueue() { m_PocketedEnemyQueue.clear(); }

private:
    static constexpr int kRequiredStoppedTicks = 11;

    GameWorld* m_World = nullptr;
    BattleControllerHooks m_Hooks{};

    BattleState m_State = BattleState::Inactive;
    BattleResult m_Result = BattleResult::None;
    bool m_Active = false;

    int m_AllBallsStoppedTickCount = 0;
    ShotRelicRules m_ShotRelicRules{};
    bool m_BountyRewardClaimed = false;
    StageType m_StageType = StageType::Normal;
    BattlePocketRules m_PocketRules{};
    std::mt19937 m_PocketRandomEngine{ std::random_device{}() };
    std::deque<EnemyBall*> m_PocketedEnemyQueue;

private:
    void CompleteShot();
    void ProcessEnemyAttack();
    void ProcessTurnEnd();

    bool TryRecoverClearedBattle(const char* source);

    void FinishBattle(BattleResult result);
};
