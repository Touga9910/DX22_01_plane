#pragma once

#include "GameTypes.h"
#include <algorithm>

// Shot-local values only. Preview owns its own copy; rewards and logs stay in Game.
struct ShotRelicRules
{
    std::array<bool, static_cast<std::size_t>(RelicType::Count)> relics{};
    std::string ballId;
    int collisionBonus = 0, playerEnemyContacts = 0, enemyEnemyContacts = 0;
    int wallContacts = 0, bounceBonus = 0;
    bool bankReady = false, bankConsumed = false, anchorStopped = false;
    float launchPower = 0.0f;
    bool Has(RelicType type) const { return relics[static_cast<std::size_t>(type)]; }
    void Wall()
    {
        ++wallContacts;
        if (Has(RelicType::BounceBallSpring) && ballId == "player_bounce")
            bounceBonus = (std::min)(3, bounceBonus + 1);
        if (Has(RelicType::BankShot) && !bankConsumed) bankReady = true;
    }
    int ConsumeBankShotDamageMultiplier()
    {
        if (!Has(RelicType::BankShot) || !bankReady || bankConsumed) return 1;
        bankReady = false;
        bankConsumed = true;
        return 2;
    }
    int ConsumePlayerEnemyRelicDamageBonus()
    {
        int bonus = 0;
        if (Has(RelicType::StandardBallScope) && ballId == "player_standard" &&
            launchPower <= 4.0001f && wallContacts == 0) ++bonus;
        if (Has(RelicType::HeavyBallCore) && ballId == "player_heavy") ++bonus;
        if (Has(RelicType::BounceBallSpring) && ballId == "player_bounce")
        {
            bonus += bounceBonus;
            bounceBonus = 0;
        }
        if (Has(RelicType::AnchorBallChain) && ballId == "player_anchor" && anchorStopped) ++bonus;
        return bonus;
    }
    void Anchor() { if (ballId == "player_anchor") anchorStopped = true; }
    void Contact(bool playerEnemy)
    {
        if (playerEnemy) ++playerEnemyContacts;
        else ++enemyEnemyContacts;
        if (Has(RelicType::CollisionAttackUp)) ++collisionBonus;
    }
    int GetCurrentShotCollisionAttackBonus() const { return collisionBonus; }
};
