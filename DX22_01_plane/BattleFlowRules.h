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

    /// <summary>
    /// 連続停止ステップ数が停止確定に必要な回数に達したかを判定する。
    /// </summary>
    constexpr bool HasConfirmedStop(int stoppedTickCount)
    {
        return stoppedTickCount >= RequiredStoppedTicks;
    }

    /// <summary>
    /// フェーズ戦を開始するため、初回フェーズ、総数、残りターン数を組み立てる。
    /// </summary>
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

    /// <summary>
    /// 後続フェーズの有無と敵の残数・残りターンから、次へ進めるかを判定する。
    /// </summary>
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

    /// <summary>
    /// 敵が配置済みの従来方式の戦闘で、生存中の敵がいなくなったかを判定する。
    /// </summary>
    constexpr bool IsLegacyVictory(bool enemiesHaveExisted, int livingEnemyCount)
    {
        return enemiesHaveExisted && livingEnemyCount == 0;
    }

    /// <summary>
    /// 最終フェーズに到達し、生存中の敵がいなくなったかを判定する。
    /// </summary>
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

    /// <summary>
    /// 敵が全滅した進行中の戦闘で、ボールの移動状態を考慮して勝利を確定できるかを判定する。
    /// </summary>
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
