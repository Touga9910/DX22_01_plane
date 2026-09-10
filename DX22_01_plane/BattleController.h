#pragma once

#include <functional>
#include <string>

class GameWorld;
class PlayerBall;

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

private:
    static constexpr int kRequiredStoppedTicks = 11;

    GameWorld* m_World = nullptr;
    BattleControllerHooks m_Hooks{};

    BattleState m_State = BattleState::Inactive;
    BattleResult m_Result = BattleResult::None;
    bool m_Active = false;

    int m_AllBallsStoppedTickCount = 0;

private:
    void CompleteShot();
    void ProcessEnemyAttack();
    void ProcessTurnEnd();

    bool TryRecoverClearedBattle(const char* source);

    void FinishBattle(BattleResult result);
};
