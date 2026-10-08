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

const char* ToString(BattleState state);
const char* ToString(BattleResult result);
