#pragma once

#include "BattleTypes.h"

#include <cstdint>

// Read-only battle values for gameplay objects and rendering components.
// Callers do not need the Game orchestrator or mutable rule state.
class GameBattleView final
{
public:
    static BattleState State();
    static bool AcceptsHumanInput();
    static bool ClearRewardActive();
    static int CushionRegionCount();
    static bool IsCushionChargeActive(int region);
    static int CurrentShotCollisionAttackBonus();
    static int PierceMaximumUses();
    static float PierceSpeedRetention();
	static int ScaleEnemyCollisionDamage(int damage);
    static bool IsCurrentShotPierceCategory();
	static bool IsCurrentShotBounceCategory();
	static bool IsPinballTarget(std::uintptr_t targetId);

    GameBattleView() = delete;
};
