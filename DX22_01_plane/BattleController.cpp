#include "BattleController.h"

#include "BattleFlowRules.h"
#include "BallPhysicsComponent.h"
#include "BreakBall.h"
#include "EnemyAttackComponent.h"
#include "EnemyBall.h"
#include "GameObject.h"
#include "GameWorld.h"
#include "PlayerBall.h"
#include "EnemyAttackTiming.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>
#include <vector>

void BattleController::ResetShotState(const ShotRelicRules& rules)
{
    m_ShotRelicRules = rules;
}

bool BattleController::ClaimBountyReward()
{
    if (m_BountyRewardClaimed)
    {
        return false;
    }
    m_BountyRewardClaimed = true;
    return true;
}

void BattleController::BeginStage(const StageData& stage)
{
    m_StageType = stage.stageType;
    m_CombatMode = stage.combatMode;
    m_PhaseBattle = stage.phaseBattle;
    const BattleFlowRules::PhaseStartState phaseStart =
        BattleFlowRules::BeginPhaseBattle(
            IsPhasePrototype(),
            static_cast<int>(m_PhaseBattle.phases.size()),
            m_PhaseBattle.turnsPerPhase);
    m_TotalPhases = phaseStart.totalPhases;
    m_CurrentPhase = phaseStart.currentPhase;
    m_TurnsUntilNextPhase = phaseStart.turnsUntilNextPhase;
    m_DefeatedEnemyCount = 0;
    m_PocketDefeatedEnemyCount = 0;
    m_BountyRewardClaimed = false;
    m_EnemyActionPlanner.Reset();
    if (m_World != nullptr)
    {
        for (EnemyBall* enemy : m_World->GetComponents<EnemyBall>())
        {
            if (enemy != nullptr) enemy->RefreshBoardAbilityState(false);
        }
    }
    if (!IsPhasePrototype()) PlanNextTurn();
}

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
    m_EnemyActionPlanner.LoadConfig(
        "assets/data/enemy_action_rules.json");
	m_DefaultEnemyActionConfig = m_EnemyActionPlanner.Config();
    Reset();
}

void BattleController::SetEnemyActionMaximumBudget(int maximumBudget)
{
	EnemyActionConfig config = m_DefaultEnemyActionConfig;
	if (maximumBudget >= 0)
	{
		config.maximumBudget = maximumBudget;
	}
	m_EnemyActionPlanner.SetConfigForTesting(config);
}

void BattleController::StartBattle()
{
    m_TurnNumber = 0;
    m_FixedStepCount = 0;
    m_ShotFixedStepCount = 0;
    m_StateHistory.clear();
    SetState(BattleState::AimingDirection, "start_battle");
    m_Result = BattleResult::None;
    m_AllBallsStoppedTickCount = 0;
    m_Active = true;
    m_EnemyActionPlanner.Reset();
}

void BattleController::Reset()
{
    m_StateHistory.clear();
    SetState(BattleState::Inactive, "reset");
    m_Result = BattleResult::None;
    m_AllBallsStoppedTickCount = 0;
    m_TurnNumber = 0;
    m_FixedStepCount = 0;
    m_ShotFixedStepCount = 0;
    m_Active = false;
    m_EnemyActionPlanner.Reset();
    m_CombatMode = CombatMode::Legacy;
    m_PhaseBattle = {};
    m_CurrentPhase = 0;
    m_TotalPhases = 0;
    m_TurnsUntilNextPhase = 0;
    m_DefeatedEnemyCount = 0;
    m_PocketDefeatedEnemyCount = 0;
}

void BattleController::NotifyEnemyDefeated()
{
    ++m_DefeatedEnemyCount;
}

void BattleController::NotifyEnemyPocketDefeated()
{
    ++m_PocketDefeatedEnemyCount;
}

void BattleController::Update(double elapsedSeconds)
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
        if (BattleFlowRules::HasConfirmedStop(
                m_AllBallsStoppedTickCount))
        {
            CompleteShot();
        }
        break;

    case BattleState::EnemyAttack:
        m_EnemyAttackDelay = EnemyAttackTiming::Advance(m_EnemyAttackDelay, elapsedSeconds);
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
    ++m_FixedStepCount;
    if (IsFinished())
    {
        return true;
    }

    if (!m_Active ||
        m_State != BattleState::BallsMoving)
    {
        return false;
    }

    ++m_ShotFixedStepCount;
    if (m_ShotFixedStepCount % 300 == 0)
    {
        LogProgressDiagnostic("moving_interval");
    }

    if (AreAllBallsStopped())
    {
        ++m_AllBallsStoppedTickCount;
        if (m_AllBallsStoppedTickCount == 1)
        {
            LogProgressDiagnostic("all_stopped_started");
        }
    }
    else
    {
        if (m_AllBallsStoppedTickCount > 0)
        {
            LogProgressDiagnostic("all_stopped_interrupted");
        }
        m_AllBallsStoppedTickCount = 0;
    }

    return BattleFlowRules::HasConfirmedStop(
        m_AllBallsStoppedTickCount);
}

void BattleController::BeginAimingDirection()
{
    if (!m_Active || IsFinished())
    {
        return;
    }

    SetState(BattleState::AimingDirection, "begin_aiming_direction");
}

void BattleController::BeginAimingPower()
{
    if (!m_Active || IsFinished())
    {
        return;
    }

    SetState(BattleState::AimingPower, "begin_aiming_power");
}

void BattleController::NotifyShotFired(bool heavyShot)
{
    if (!m_Active || IsFinished())
    {
        return;
    }

    m_AllBallsStoppedTickCount = 0;
    ++m_TurnNumber;
    m_ShotFixedStepCount = 0;
    if (m_World != nullptr)
    {
        for (EnemyBall* enemy : m_World->GetComponents<EnemyBall>())
        {
            if (enemy != nullptr)
            {
                enemy->BeginChargeShotTracking(heavyShot);
            }
        }
    }
    SetState(BattleState::BallsMoving, "shot_fired");
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

    const int livingEnemyCount = static_cast<int>(std::count_if(
        enemies.begin(), enemies.end(), [](const EnemyBall* enemy)
        {
            return enemy != nullptr && enemy->GetHP() > 0 && !enemy->IsDefeated();
        }));
    if (IsPhasePrototype())
    {
        return IsPhaseVictoryForLivingCount(livingEnemyCount);
    }

    // Pocketed enemies disappear from the world. A cumulative defeat proves
    // this is a cleared battle rather than an empty board during loading.
    return BattleFlowRules::IsLegacyVictory(
        !enemies.empty() || m_DefeatedEnemyCount > 0, livingEnemyCount);
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

    ResolveChargeInterruptions();

    if (IsPhasePrototype())
    {
        // HP0 enemies remain physical across turns; only pocket contact removes them.
        if (m_CurrentPhase < m_TotalPhases && m_TurnsUntilNextPhase > 0)
        {
            --m_TurnsUntilNextPhase;
        }
        PlanPhaseTurn();
    }

    // Positions are now stable. Re-evaluate only board-dependent modifiers;
    // the already committed action type and budget remain untouched.
    for (EnemyBall* enemy : m_World->GetComponents<EnemyBall>())
    {
        if (enemy != nullptr) enemy->RefreshBoardAbilityState(true);
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
            GetDefeatedEnemyCount());
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

    SetState(BattleState::EnemyAttack, "shot_completed");
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

    PlayerBall* player = players.front();
    player->RefreshNuisanceDebuffs();

    if (m_EnemyAttackDelay > 0.0) return;
    if (!m_EnemyAttackStarted)
    {
        m_EnemyAttackStarted = true;
        m_EnemyAttackPredicted = m_Hooks.beginEnemyAttackForecast
            ? m_Hooks.beginEnemyAttackForecast() : 0;
        m_EnemyAttackHpBefore = player->GetHP();
    }
    for (; m_EnemyAttackCursor < enemies.size(); ++m_EnemyAttackCursor)
    {
        EnemyBall* enemy = enemies[m_EnemyAttackCursor];
        if (enemy == nullptr ||
            !enemy->CanParticipateInCombat() ||
            enemy->IsStunned())
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

        const EnemyActionType action = enemy->GetPlannedAction();
        if (action == EnemyActionType::Wait)
        {
            continue;
        }
        if (action == EnemyActionType::ChargeStart)
        {
            enemy->BeginCharge();
            if (IsPhasePrototype()) enemy->ResetAttackTimer();
            continue;
        }
        if (action == EnemyActionType::ChargeAttack &&
            !enemy->IsCharging())
        {
            continue;
        }
        if (action != EnemyActionType::NormalAttack &&
            action != EnemyActionType::ChargeAttack)
        {
            continue;
        }

        if (m_Hooks.enemyAttackLaunched && !m_EnemyAttackLaunched)
        {
            m_EnemyAttackLaunched = true;
            m_EnemyAttackDelay = EnemyAttackTiming::FlightSeconds;
            m_Hooks.enemyAttackLaunched(*enemy, *player);
            return;
        }
        m_EnemyAttackLaunched = false;
        const int shieldBefore = m_Hooks.playerShield ? m_Hooks.playerShield() : 0;
        const int hpBefore = player->GetHP();

        attack->Attack(
            player,
            enemy->GetPlannedDamageMultiplier());

        if (action == EnemyActionType::ChargeAttack)
        {
            enemy->CompleteChargeAttack();
        }
        if (IsPhasePrototype()) enemy->ResetAttackTimer();

        const int hpAfter = player->GetHP();
        const int damage =
            (std::max)(0, hpBefore - hpAfter);

        if (m_Hooks.enemyAttackImpact)
            m_Hooks.enemyAttackImpact(damage, (std::max)(0, shieldBefore -
                (m_Hooks.playerShield ? m_Hooks.playerShield() : 0)));
        if (m_Hooks.notifyPlayerDamage)
        {
            m_Hooks.notifyPlayerDamage(
                action == EnemyActionType::ChargeAttack
                    ? "enemy_charge_attack"
                    : "enemy_attack",
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
        if (m_Hooks.enemyAttackLaunched)
        {
            ++m_EnemyAttackCursor;
            m_EnemyAttackDelay = EnemyAttackTiming::GapSeconds;
            return;
        }
    }

    const int actualDamage =
        (std::max)(
            0,
            m_EnemyAttackHpBefore - player->GetHP());

    if (m_Hooks.endEnemyAttackForecast)
    {
        m_Hooks.endEnemyAttackForecast(
            m_EnemyAttackPredicted,
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

    SetState(BattleState::TurnEnd, "enemy_attack_completed");
}

void BattleController::ProcessTurnEnd()
{
    if (m_World != nullptr)
    {
        for (EnemyBall* enemy : m_World->GetComponents<EnemyBall>())
        {
            if (enemy != nullptr) enemy->AdvanceTurnGimmicks();
        }
    }

    if (IsPhasePrototype())
    {
        TryAdvancePhase();
        if (AreAllEnemiesDefeated())
        {
            FinishBattle(BattleResult::Victory);
            return;
        }
    }

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

    if (!IsPhasePrototype()) PlanNextTurn();
    if (IsPhasePrototype() && m_World != nullptr)
    {
        for (EnemyBall* enemy : m_World->GetComponents<EnemyBall>())
        {
            if (enemy != nullptr && enemy->CanParticipateInCombat())
            {
                enemy->SetPlannedAction(
                    EnemyActionType::Wait, 0,
                    m_EnemyActionPlanner.Config().chargeDamageMultiplier);
            }
        }
    }
    SetState(BattleState::AimingDirection, "turn_completed");
}

void BattleController::PlanPhaseTurn()
{
    if (m_World == nullptr || !IsPhasePrototype()) return;

    EnemyActionTurnPlan plan{};
    for (EnemyBall* enemy : m_World->GetComponents<EnemyBall>())
    {
        if (enemy == nullptr || !enemy->CanParticipateInCombat()) continue;

        EnemyActionType action = EnemyActionType::Wait;
        if (!enemy->IsStunned() && enemy->AdvanceAttackTimer())
        {
            if (enemy->CanUseCharge())
            {
                action = enemy->IsCharging()
                    ? EnemyActionType::ChargeAttack
                    : EnemyActionType::ChargeStart;
            }
            else
            {
                action = EnemyActionType::NormalAttack;
            }
        }
        enemy->SetPlannedAction(
            action, 0, m_EnemyActionPlanner.Config().chargeDamageMultiplier);
        plan.actions.push_back({
            reinterpret_cast<std::uintptr_t>(enemy), action, 0 });
    }
    if (m_Hooks.enemyActionPlanConfirmed)
    {
        m_Hooks.enemyActionPlanConfirmed(plan);
    }
}

bool BattleController::TryAdvancePhase()
{
    return TryAdvancePhase(GetLivingEnemyCount());
}

bool BattleController::TryAdvancePhase(int livingEnemyCount)
{
    const bool noLivingEnemies = livingEnemyCount <= 0;
    if (!BattleFlowRules::CanAdvancePhase(
            IsPhasePrototype(),
            m_CurrentPhase,
            m_TotalPhases,
            noLivingEnemies,
            m_TurnsUntilNextPhase))
    {
        return false;
    }

    const int nextIndex = m_CurrentPhase;
    if (nextIndex < 0 ||
        nextIndex >= static_cast<int>(m_PhaseBattle.phases.size()))
    {
        return false;
    }
    if (m_Hooks.spawnPhaseEnemies)
    {
        m_Hooks.spawnPhaseEnemies(
            m_PhaseBattle.phases[static_cast<std::size_t>(nextIndex)].enemies,
            m_PhaseBattle.defaultAttackInterval,
            m_PhaseBattle.spawnSeed,
            nextIndex + 1);
    }
    ++m_CurrentPhase;
    m_TurnsUntilNextPhase = m_CurrentPhase < m_TotalPhases
        ? m_PhaseBattle.turnsPerPhase
        : 0;
    return true;
}

bool BattleController::IsPhaseVictoryForLivingCount(
    int livingEnemyCount) const
{
    return BattleFlowRules::IsPhaseVictory(
        IsPhasePrototype(),
        m_CurrentPhase,
        m_TotalPhases,
        livingEnemyCount);
}

int BattleController::GetLivingEnemyCount() const
{
    if (m_World == nullptr) return 0;
    const std::vector<EnemyBall*> enemies =
        m_World->GetComponents<EnemyBall>();
    return static_cast<int>(std::count_if(
        enemies.begin(), enemies.end(),
        [](const EnemyBall* enemy)
        {
            return enemy != nullptr && enemy->GetHP() > 0 && !enemy->IsDefeated();
        }));
}

int BattleController::GetDefeatedEnemyCount() const
{
    return m_DefeatedEnemyCount;
}

int BattleController::GetNextPhaseEnemyCount() const
{
    if (!IsPhasePrototype() || m_CurrentPhase >= m_TotalPhases ||
        m_CurrentPhase < 0 ||
        m_CurrentPhase >= static_cast<int>(m_PhaseBattle.phases.size()))
    {
        return 0;
    }
    return static_cast<int>(m_PhaseBattle.phases[
        static_cast<std::size_t>(m_CurrentPhase)].enemies.size());
}

void BattleController::PlanNextTurn()
{
    if (m_World == nullptr)
    {
        return;
    }

    const std::vector<EnemyBall*> enemies =
        m_World->GetComponents<EnemyBall>();
    std::vector<EnemyBall*> participatingEnemies;
    std::vector<EnemyActionCandidate> candidates;
    candidates.reserve(enemies.size());
    for (EnemyBall* enemy : enemies)
    {
        if (enemy == nullptr || !enemy->CanParticipateInCombat())
        {
            if (enemy != nullptr)
            {
                enemy->SetPlannedAction(
                    EnemyActionType::Wait,
                    0,
                    m_EnemyActionPlanner.Config().chargeDamageMultiplier);
            }
            continue;
        }

        bool canAct = false;
        if (!enemy->IsStunned())
        {
            GameObject* owner = enemy->GetGameObject();
            canAct = owner != nullptr &&
                owner->GetComponent<EnemyAttackComponent>() != nullptr;
        }
        participatingEnemies.push_back(enemy);
        candidates.push_back({
            reinterpret_cast<std::uintptr_t>(enemy),
            canAct,
            enemy->IsCharging(),
            enemy->CanUseCharge(),
        });
    }

    const EnemyActionTurnPlan plan =
        m_EnemyActionPlanner.PlanTurn(candidates);
    for (std::size_t index = 0; index < participatingEnemies.size(); ++index)
    {
        EnemyBall* enemy = participatingEnemies[index];
        const PlannedEnemyAction& assignment = plan.actions[index];
        enemy->SetPlannedAction(
            assignment.action,
            assignment.cost,
            m_EnemyActionPlanner.Config().chargeDamageMultiplier);
    }

    if (m_Hooks.enemyActionPlanConfirmed)
    {
        m_Hooks.enemyActionPlanConfirmed(plan);
    }
}

void BattleController::ResolveChargeInterruptions()
{
    if (m_World == nullptr)
    {
        return;
    }
    const EnemyActionConfig& config = m_EnemyActionPlanner.Config();
    for (EnemyBall* enemy : m_World->GetComponents<EnemyBall>())
    {
        if (enemy == nullptr)
        {
            continue;
        }
        const std::optional<ChargeInterruptionResult> result =
            enemy->EndChargeShotTracking(
                config.heavyInterruptDiameterMultiplier,
                config.pierceInterruptMaxHpRatio);
        if (result.has_value() &&
            result->WasInterrupted() &&
            m_Hooks.chargeInterrupted)
        {
            m_Hooks.chargeInterrupted(*enemy, *result);
        }
    }
}

bool BattleController::TryRecoverClearedBattle(
    const char* source)
{
    if (!BattleFlowRules::CanRecoverClearedBattle(
            m_Active,
            IsFinished(),
            m_State,
            AreAllEnemiesDefeated()))
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
    SetState(BattleState::Finished, ToString(result));
    m_AllBallsStoppedTickCount = 0;
    m_Active = false;
}

void BattleController::SetState(BattleState state, const char* reason)
{
    if (m_State == state && !m_StateHistory.empty())
    {
        return;
    }

    if (state == BattleState::EnemyAttack)
    {
        m_EnemyAttackCursor = 0;
        m_EnemyAttackStarted = m_EnemyAttackLaunched = false;
        m_EnemyAttackDelay = 0.0;
    }
    const BattleState previous = m_State;
    m_State = state;
    m_StateHistory.push_back(state);
    constexpr std::size_t kMaximumStateHistory = 12;
    while (m_StateHistory.size() > kMaximumStateHistory)
    {
        m_StateHistory.pop_front();
    }

    std::clog << "[BattleProgress] state " << ToString(previous)
        << " -> " << ToString(state)
        << " reason=" << (reason != nullptr ? reason : "unknown")
        << " turn=" << m_TurnNumber
        << " fixed_steps=" << m_FixedStepCount << '\n';
    LogProgressDiagnostic("state_transition");
}

void BattleController::LogProgressDiagnostic(const char* reason) const
{
    if (m_World == nullptr)
    {
        return;
    }

    const std::vector<EnemyBall*> enemies =
        m_World->GetComponents<EnemyBall>();
    const std::vector<GameObject*> physicsObjects =
        m_World->GetObjectsWith<BallPhysicsComponent>();
    std::ostringstream moving;
    moving << std::fixed << std::setprecision(4);
    int movingCount = 0;
    int activePhysicsCount = 0;
    for (GameObject* object : physicsObjects)
    {
        if (object == nullptr)
        {
            continue;
        }
        if (object->IsActive())
        {
            ++activePhysicsCount;
        }
        const BallPhysicsComponent* physics =
            object->GetComponent<BallPhysicsComponent>();
        if (physics == nullptr || physics->IsStopped())
        {
            continue;
        }

        const auto& velocity = physics->GetVelocity();
        if (movingCount++ > 0)
        {
            moving << ';';
        }
        moving << object->GetName();
        if (const EnemyBall* enemy = object->GetComponent<EnemyBall>())
        {
            moving << '(' << enemy->GetEnemyId()
                << (enemy->IsDefeated() ? ",defeated" : ",alive") << ')';
        }
        moving << "@" << static_cast<const void*>(object)
            << " v=(" << velocity.x << ',' << velocity.y << ','
            << velocity.z << ')';
    }

    std::ostringstream history;
    for (std::size_t index = 0; index < m_StateHistory.size(); ++index)
    {
        if (index > 0)
        {
            history << '>';
        }
        history << ToString(m_StateHistory[index]);
    }

    std::clog << "[BattleProgress] reason="
        << (reason != nullptr ? reason : "unknown")
        << " state=" << ToString(m_State)
        << " turn=" << m_TurnNumber
        << " alive=" << CountAliveEnemies(enemies)
        << " defeated=" << CountDefeatedEnemies(enemies)
        << " physics=" << physicsObjects.size()
        << " active_physics=" << activePhysicsCount
        << " moving=" << movingCount
        << " stopped_ticks=" << m_AllBallsStoppedTickCount
        << " fixed_steps=" << m_FixedStepCount
        << " shot_fixed_steps=" << m_ShotFixedStepCount
        << " history=" << history.str();
    if (movingCount > 0)
    {
        std::clog << " non_stopped=[" << moving.str() << ']';
    }
    std::clog << '\n';
}
