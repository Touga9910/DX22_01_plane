#pragma once

#include "AnchorStackRules.h"
#include "BattleController.h"
#include "CushionChargeRules.h"
#include "HeavyCollisionRules.h"
#include "PierceTraceRules.h"

#include <unordered_set>

class EnemyBall;

// Mutable state that exists only while resolving a battle or a shot.
// Game owns this aggregate and remains the orchestration boundary, while
// consumers of Game.h no longer parse the individual battle rule types.
struct GameBattleState final
{
    BattleController controller;
    CushionChargeRules::State cushionCharges{};
    bool cushionBoostConsumedThisShot = false;
    int cushionStrongUsesThisShot = 0;
    int synergyDamageBonusThisShot = 0;
    HeavyCollisionRules::State heavyCollisions{};
    PierceTraceRules::State pierceTraces{};
    AnchorStackRules::State anchorStacks{};
    bool heavyFinisherConsumedThisShot = false;
	bool anchorFinisherTriggeredThisShot = false;
    EnemyBall* anchorContactTarget = nullptr;
    std::unordered_set<const EnemyBall*> piercedEnemiesThisShot;
    int playerShield = 0;
};
