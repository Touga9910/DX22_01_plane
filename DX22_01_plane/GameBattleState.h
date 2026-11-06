#pragma once

#include "AnchorStackRules.h"
#include "BattleController.h"
#include "CushionChargeRules.h"
#include "HeavyCollisionRules.h"
#include "PierceTraceRules.h"

#include <array>
#include <cstdint>
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
	bool pierceFinisherPresentedThisShot = false;
	bool anchorFinisherTriggeredThisShot = false;
    EnemyBall* anchorContactTarget = nullptr;
    std::unordered_set<const EnemyBall*> piercedEnemiesThisShot;
    int playerShield = 0;

	// Values collected only for the end-of-shot presentation snapshot.
	// Physics, damage, and rule decisions never read these fields.
	int presentationHitCount = 0;
	int presentationDamage = 0;
	int presentationDefeatCount = 0;
	int presentationChargeInterruptions = 0;
	std::array<int, 4> presentationResourceGained{};
	std::array<int, 4> presentationResourceConsumed{};
	std::uint32_t presentationFinisherMask = 0;
};
