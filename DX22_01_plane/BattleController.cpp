#include "BattleController.h"

#include "BallPhysicsComponent.h"
#include "BreakBall.h"
#include "EnemyAttackComponent.h"
#include "EnemyBall.h"
#include "GameObject.h"
#include "GameWorld.h"
#include "PlayerBall.h"

#include <algorithm>
#include <utility>
#include <vector>

const char* ToString(BattleState state)
{
    switch (state)
    {
    case BattleState::Inactive:
        return "inactive";
    case BattleState::AimingDirection:
        return "aiming_direction";
    case BattleState::AimingPower:
        return "aiming_power";
    case BattleState::ConfirmShot:
        return "confirm_shot";
    case BattleState::BallsMoving:
        return "balls_moving";
    case BattleState::EnemyAttack:
        return "enemy_attack";
    case BattleState::TurnEnd:
        return "turn_end";
    case BattleState::Finished:
        return "finished";
    default:
        return "unknown";
    }
}

const char* ToString(BattleResult result)
{
    switch (result)
    {
    case BattleResult::None:
        return "none";
    case BattleResult::Victory:
        return "victory";
    case BattleResult::Defeat:
        return "defeat";
    default:
        return "unknown";
    }
}

namespace
{
    int CountAliveEnemies(
        const std::vector<EnemyBall*>& enemies)
    {
        return static_cast<int>(
            std::count_if(
                enemies.begin(),
                enemies.end(),
                [](const EnemyBall* enemy)
                {
                    return enemy != nullptr &&
                           !enemy->IsDefeated();
                }));
    }

    int CountDefeatedEnemies(
        const std::vector<EnemyBall*>& enemies)
    {
        return static_cast<int>(
            std::count_if(
                enemies.begin(),
                enemies.end(),
                [](const EnemyBall* enemy)
                {
                    return enemy != nullptr &&
                           enemy->IsDefeated();
                }));
    }
}

void BattleController::Initialize(
    GameWorld& world,
    BattleControllerHooks hooks)
{
    m_World = &world;
    m_Hooks = std::move(hooks);
    Reset();
}

void BattleController::StartBattle()
{
    m_State = BattleState::AimingDirection;
    m_Result = BattleResult::None;
    m_AllBallsStoppedTickCount = 0;
    m_Active = true;
}

void BattleController::Reset()
{
    m_State = BattleState::Inactive;
    m_Result = BattleResult::None;
    m_AllBallsStoppedTickCount = 0;
    m_Active = false;
}

void BattleController::Update()
{
    if (!m_Active ||
        m_World == nullptr ||
        IsFinished())
    {
        return;
    }

    if (TryRecoverClearedBattle("state_invariant"))
    {
        return;
    }

    switch (m_State)
    {
    case BattleState::BallsMoving:
        if (m_AllBallsStoppedTickCount >= kRequiredStoppedTicks)
        {
            CompleteShot();
        }
        break;

    case BattleState::EnemyAttack:
        ProcessEnemyAttack();
        break;

    case BattleState::TurnEnd:
        ProcessTurnEnd();
        break;

    default:
        break;
    }
}

bool BattleController::OnFixedStepCompleted()
{
    if (IsFinished())
    {
        return true;
    }

    if (!m_Active ||
        m_State != BattleState::BallsMoving)
    {
        return false;
    }

    if (AreAllBallsStopped())
    {
        ++m_AllBallsStoppedTickCount;
    }
    else
    {
        m_AllBallsStoppedTickCount = 0;
    }

    return m_AllBallsStoppedTickCount >=
           kRequiredStoppedTicks;
}

void BattleController::BeginAimingDirection()
{
    if (!m_Active || IsFinished())
    {
        return;
    }

    m_State = BattleState::AimingDirection;
}

void BattleController::BeginAimingPower()
{
    if (!m_Active || IsFinished())
    {
        return;
    }

    m_State = BattleState::AimingPower;
}

void BattleController::BeginConfirmShot()
{
    if (!m_Active || IsFinished())
    {
        return;
    }

    m_State = BattleState::ConfirmShot;
}

void BattleController::NotifyShotFired()
{
    if (!m_Active || IsFinished())
    {
        return;
    }

    m_AllBallsStoppedTickCount = 0;
    m_State = BattleState::BallsMoving;
}

void BattleController::NotifyPlayerDefeated()
{
    if (!m_Active || IsFinished())
    {
        return;
    }

    FinishBattle(BattleResult::Defeat);
}

BattleResult BattleController::ConsumeResult()
{
    const BattleResult result = m_Result;
    m_Result = BattleResult::None;
    return result;
}

bool BattleController::AreAllBallsStopped() const
{
    if (m_World == nullptr)
    {
        return false;
    }

    const std::vector<GameObject*> balls =
        m_World->GetObjectsWith<BallPhysicsComponent>();

    if (balls.empty())
    {
        return false;
    }

    for (GameObject* object : balls)
    {
        if (object == nullptr)
        {
            continue;
        }

        BallPhysicsComponent* physics =
            object->GetComponent<BallPhysicsComponent>();

        // 撃破済みの敵も反射後に停止するまでは
        // ショット中として扱う。
        if (physics != nullptr &&
            !physics->IsStopped())
        {
            return false;
        }
    }

    return true;
}

bool BattleController::AreAllEnemiesDefeated() const
{
    if (m_World == nullptr)
    {
        return false;
    }

    const std::vector<EnemyBall*> enemies =
        m_World->GetComponents<EnemyBall>();

    // 敵が存在しないロード途中を勝利扱いにしない。
    if (enemies.empty())
    {
        return false;
    }

    bool foundEnemy = false;

    for (EnemyBall* enemy : enemies)
    {
        if (enemy == nullptr)
        {
            continue;
        }

        foundEnemy = true;

        if (!enemy->IsDefeated())
        {
            return false;
        }
    }

    return foundEnemy;
}

void BattleController::CompleteShot()
{
    if (m_World == nullptr)
    {
        return;
    }

    m_AllBallsStoppedTickCount = 0;

    // 11 FixedTick停止を確認した後、誤差を残さないよう
    // 物理速度・加速度を完全に0へする。
    for (GameObject* object :
        m_World->GetObjectsWith<BallPhysicsComponent>())
    {
        if (object == nullptr)
        {
            continue;
        }

        BallPhysicsComponent* physics =
            object->GetComponent<BallPhysicsComponent>();

        if (physics == nullptr)
        {
            continue;
        }

        physics->Velocity() =
            DirectX::SimpleMath::Vector3::Zero;
        physics->Acceleration() =
            DirectX::SimpleMath::Vector3::Zero;
    }

    if (m_Hooks.restorePocketedPlayer)
    {
        m_Hooks.restorePocketedPlayer();
    }

    const std::vector<PlayerBall*> players =
        m_World->GetComponents<PlayerBall>();

    const std::vector<EnemyBall*> enemies =
        m_World->GetComponents<EnemyBall>();

    if (players.empty() ||
        players.front() == nullptr)
    {
        FinishBattle(BattleResult::Defeat);
        return;
    }

    PlayerBall* player = players.front();

    if (m_Hooks.applyEndOfShotEffects)
    {
        m_Hooks.applyEndOfShotEffects(player);
    }

    if (m_Hooks.endShotLog)
    {
        m_Hooks.endShotLog(
            player->GetHP(),
            CountAliveEnemies(enemies),
            CountDefeatedEnemies(enemies));
    }

    if (m_Hooks.finishDynamicBalanceShot)
    {
        m_Hooks.finishDynamicBalanceShot();
    }

    for (EnemyBall* enemy : enemies)
    {
        if (enemy != nullptr)
        {
            enemy->EndBossShot();
        }
    }

    if (!AreAllEnemiesDefeated())
    {
        for (BreakBall* breakBall :
            m_World->GetComponents<BreakBall>())
        {
            if (breakBall != nullptr)
            {
                breakBall->Reposition();
            }
        }
    }

    if (AreAllEnemiesDefeated())
    {
        FinishBattle(BattleResult::Victory);
        return;
    }

    m_State = BattleState::EnemyAttack;
}

void BattleController::ProcessEnemyAttack()
{
    if (m_World == nullptr)
    {
        return;
    }

    std::vector<PlayerBall*> players =
        m_World->GetComponents<PlayerBall>();

    std::vector<EnemyBall*> enemies =
        m_World->GetComponents<EnemyBall>();

    if (players.empty() ||
        players.front() == nullptr)
    {
        FinishBattle(BattleResult::Defeat);
        return;
    }

    // 撃破済みの敵はショット中の反射物として残し、
    // 敵攻撃開始直前にのみ削除要求を出す。
    for (EnemyBall* enemy : enemies)
    {
        if (enemy == nullptr ||
            !enemy->IsDefeated())
        {
            continue;
        }

        GameObject* owner = enemy->GetGameObject();
        if (owner != nullptr)
        {
            m_World->RequestDestroy(owner);
        }
    }

    // Destroy要求済みオブジェクトはGameWorld検索から除外される。
    enemies = m_World->GetComponents<EnemyBall>();

    PlayerBall* player = players.front();

    int predictedDamage = 0;
    if (m_Hooks.beginEnemyAttackForecast)
    {
        predictedDamage =
            m_Hooks.beginEnemyAttackForecast();
    }

    const int hpBeforeEnemyAttack =
        player->GetHP();

    for (EnemyBall* enemy : enemies)
    {
        if (enemy == nullptr ||
            enemy->IsDefeated() ||
            enemy->IsPocketed())
        {
            continue;
        }

        GameObject* owner = enemy->GetGameObject();
        if (owner == nullptr)
        {
            continue;
        }

        EnemyAttackComponent* attack =
            owner->GetComponent<EnemyAttackComponent>();

        if (attack == nullptr)
        {
            continue;
        }

        const int hpBefore = player->GetHP();

        attack->Attack(player);

        const int hpAfter = player->GetHP();
        const int damage =
            (std::max)(0, hpBefore - hpAfter);

        if (m_Hooks.notifyPlayerDamage)
        {
            m_Hooks.notifyPlayerDamage(
                "enemy_attack",
                damage,
                enemy->GetEnemyId(),
                hpBefore,
                hpAfter);
        }

        // TakeDamage内などから敗北通知が来た場合、
        // 残りの敵は追加攻撃しない。
        if (IsFinished() ||
            hpAfter <= 0 ||
            player->IsDefeated())
        {
            break;
        }
    }

    const int actualDamage =
        (std::max)(
            0,
            hpBeforeEnemyAttack - player->GetHP());

    if (m_Hooks.endEnemyAttackForecast)
    {
        m_Hooks.endEnemyAttackForecast(
            predictedDamage,
            actualDamage);
    }

    if (m_Hooks.capturePlayerStatus)
    {
        m_Hooks.capturePlayerStatus(player);
    }

    if (IsFinished() ||
        player->GetHP() <= 0 ||
        player->IsDefeated())
    {
        FinishBattle(BattleResult::Defeat);
        return;
    }

    if (m_Hooks.restoreNextPocketedEnemy)
    {
        m_Hooks.restoreNextPocketedEnemy();
    }

    m_State = BattleState::TurnEnd;
}

void BattleController::ProcessTurnEnd()
{
    if (!m_Hooks.prepareNextTurn)
    {
        FinishBattle(BattleResult::Defeat);
        return;
    }

    if (!m_Hooks.prepareNextTurn())
    {
        FinishBattle(BattleResult::Defeat);
        return;
    }

    m_State = BattleState::AimingDirection;
}

bool BattleController::TryRecoverClearedBattle(
    const char* source)
{
    if (!m_Active ||
        IsFinished() ||
        m_State == BattleState::BallsMoving ||
        !AreAllEnemiesDefeated())
    {
        return false;
    }

    const BattleState previousState = m_State;

    if (m_Hooks.onClearStateRecovered)
    {
        m_Hooks.onClearStateRecovered(
            source,
            previousState);
    }

    FinishBattle(BattleResult::Victory);
    return true;
}

void BattleController::FinishBattle(
    BattleResult result)
{
    if (result == BattleResult::None ||
        IsFinished())
    {
        return;
    }

    m_Result = result;
    m_State = BattleState::Finished;
    m_AllBallsStoppedTickCount = 0;
    m_Active = false;
}
