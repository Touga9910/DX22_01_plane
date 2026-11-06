#pragma once

// One battle's internal progression state. ClearReward and GameOver are
// intentionally outside this state machine.
enum class BattleState
{
    Inactive,

    AimingDirection,
    AimingPower,

    BallsMoving,
    EnemyAttack,
    TurnEnd,

    Finished,
};

enum class BattleResult
{
    None,
    Victory,
    Defeat,
};

// An enemy's action is decided before the player shoots and remains fixed
// until the following enemy phase.  Wait also represents an action that was
// cancelled after its budget had already been committed.
enum class EnemyActionType
{
    Wait,
    NormalAttack,
    ChargeStart,
    ChargeAttack,
};

enum class EnemyChargeState
{
    Normal,
    Charging,
};

// The damage caller supplies only the gameplay axis.  EnemyBall therefore
// never needs to know which concrete player/minion component caused it.
enum class EnemyDamageKind
{
    Other,
    PierceAxis,
};

enum class ChargeInterruptReason
{
    None,
    Movement,
    PierceDamage,
    Pocketed,
    Defeated,
};

struct ChargeInterruptionResult
{
    ChargeInterruptReason reason = ChargeInterruptReason::None;
    float movementDistance = 0.0f;
    float movementThreshold = 0.0f;
    int pierceDamage = 0;
    float pierceDamageThreshold = 0.0f;

    bool WasInterrupted() const
    {
        return reason != ChargeInterruptReason::None;
    }
};

const char* ToString(BattleState state);
const char* ToString(BattleResult result);

inline const char* ToString(EnemyActionType action)
{
    switch (action)
    {
    case EnemyActionType::NormalAttack: return "normal_attack";
    case EnemyActionType::ChargeStart: return "charge_start";
    case EnemyActionType::ChargeAttack: return "charge_attack";
    default: return "wait";
    }
}

inline const char* ToString(EnemyChargeState state)
{
    return state == EnemyChargeState::Charging
        ? "charging"
        : "normal";
}

inline const char* ToString(ChargeInterruptReason reason)
{
    switch (reason)
    {
    case ChargeInterruptReason::Movement: return "movement";
    case ChargeInterruptReason::PierceDamage: return "pierce_damage";
    case ChargeInterruptReason::Pocketed: return "pocketed";
    case ChargeInterruptReason::Defeated: return "defeated";
    default: return "none";
    }
}
