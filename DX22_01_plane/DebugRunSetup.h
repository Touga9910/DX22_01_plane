#pragma once
#include "RunProgressController.h"
#include <string>

// Debug jumps create a valid remaining route; skipped areas grant no rewards.
enum class DebugRunDestination { Route, Battle, MidBoss, Shop, Rest, Preparation, FinalBoss, Event };

struct DebugRunSetup
{
    int completedAreas = 0;
    DebugRunDestination destination = DebugRunDestination::Route;
    RunCombatMode combatMode = RunCombatMode::Legacy;
    bool infiniteMoney = false;
    std::string stageId;
    std::string eventId;

    bool UsesFinalBossRoute() const
    {
        return destination == DebugRunDestination::Preparation ||
            destination == DebugRunDestination::FinalBoss;
    }

    /// <summary>
    /// 指定した遷移先とルートのシードを使い、デバッグ用のラン進行状態を組み立てる。
    /// </summary>
    RunProgressState BuildProgress(std::uint32_t routeSeed) const
    {
        const bool endRoute = UsesFinalBossRoute();
        const int area = endRoute ? kNormalRouteAreaGoal :
            std::clamp(completedAreas, 0, kNormalRouteAreaGoal - 1);
        RunProgressState state;
        state.areaProgress = area;
        state.map.Generate(routeSeed, area, kNormalRouteAreaGoal - area);
        if (endRoute)
        {
            state.map.Choose(0);
            state.phase = RunPhase::BossPreparation;
            if (destination == DebugRunDestination::FinalBoss)
            {
                state.map.CompleteActive();
                state.map.Choose(1);
                state.phase = RunPhase::FinalBossReady;
            }
        }
        else if (destination == DebugRunDestination::Event)
        {
            state.map.SetDebugEventEntry();
        }
        else if (destination != DebugRunDestination::Route)
        {
            const StageRouteType type = destination == DebugRunDestination::MidBoss ? StageRouteType::MidBoss :
                destination == DebugRunDestination::Shop ? StageRouteType::Shop :
                destination == DebugRunDestination::Rest ? StageRouteType::RestSite : StageRouteType::NormalBattle;
            state.map.MigrateEntry(type);
        }
        return state;
    }
};
