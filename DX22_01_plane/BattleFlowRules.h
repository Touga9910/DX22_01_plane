#pragma once

#include "BattleTypes.h"

namespace BattleFlowRules
{
    inline constexpr int RequiredStoppedTicks = 11;

    struct PhaseStartState
    {
        int currentPhase = 0;
        int totalPhases = 0;
        int turnsUntilNextPhase = 0;
    };

    constexpr bool HasConfirmedStop(int stoppedTickCount)
    {
        return stoppedTickCount >= RequiredStoppedTicks;
    }

    constexpr PhaseStartState BeginPhaseBattle(
        bool phaseBattle,
        int configuredPhaseCount,
        int turnsPerPhase)
    {
        if (!phaseBattle || configuredPhaseCount <= 0)
        {
            return {};
        }
        return {
            1,
            configuredPhaseCount,
            configuredPhaseCount > 1 ? turnsPerPhase : 0,
        };
    }

    constexpr bool CanAdvancePhase(
        bool phaseBattle,
        int currentPhase,
        int totalPhases,
        bool noLivingEnemies,
        int turnsUntilNextPhase)
    {
        if (!phaseBattle || currentPhase >= totalPhases)
        {
            return false;
        }
        return noLivingEnemies || turnsUntilNextPhase <= 0;
    }

    constexpr bool IsLegacyVictory(bool enemiesHaveExisted, int livingEnemyCount)
    {
        return enemiesHaveExisted && livingEnemyCount == 0;
    }

    constexpr bool IsPhaseVictory(
        bool phaseBattle,
        int currentPhase,
        int totalPhases,
        int livingEnemyCount)
    {
        return phaseBattle &&
            totalPhases > 0 &&
            currentPhase >= totalPhases &&
            livingEnemyCount == 0;
    }

    constexpr bool CanRecoverClearedBattle(
        bool active,
        bool finished,
        BattleState state,
        bool allEnemiesDefeated)
    {
        return active &&
            !finished &&
            state != BattleState::BallsMoving &&
            allEnemiesDefeated;
    }
}
