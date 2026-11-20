#pragma once

#include <algorithm>

// Value-only combat state shared by live play and disposable shot predictions.
namespace BossCombatRules
{
    constexpr int MaxArmor = 2;
    constexpr int BreakShots = 2;
    constexpr int BreakBallDamage = 4;
    constexpr int PocketDamagePercent = 25;
    struct State
    {
        int armor = MaxArmor;
        int shotsRemaining = 0;
        bool startedThisShot = false;
        bool pocketResolvedThisShot = false;
        bool IsBroken() const { return shotsRemaining > 0; }
        void BeginShot() { startedThisShot = false; pocketResolvedThisShot = false; }
        bool HitBreakBall()
        {
            if (IsBroken()) return false; // Fixed damage still applies; no duration refresh.
            armor = (std::max)(0, armor - 1);
            if (armor != 0) return false;
            shotsRemaining = BreakShots;
            startedThisShot = true;
            return true;
        }
        bool EndShot()
        {
            if (!IsBroken() || startedThisShot) return false;
            if (--shotsRemaining != 0) return false;
            armor = MaxArmor;
            return true;
        }
    };

    inline int PocketDamage(int maximumHp)
    {
        // ceil(maxHp * percent / 100), without overflowing intermediate products.
        const int hp = (std::max)(0, maximumHp);
        return (std::max)(1, hp / 100 * PocketDamagePercent +
            (hp % 100 * PocketDamagePercent + 99) / 100);
    }

    struct PocketResult
    {
        bool applied = false;
        int armorRemoved = 0;
        int damage = 0;
        bool breakStarted = false;
    };

    inline PocketResult ResolvePocket(State& state, int hp, int maximumHp)
    {
        if (hp <= 0 || state.pocketResolvedThisShot) return {};
        state.pocketResolvedThisShot = true;
        PocketResult result;
        result.applied = true;
        if (state.IsBroken())
            result.damage = (std::min)(hp, PocketDamage(maximumHp));
        else
        {
            const int before = state.armor;
            result.breakStarted = state.HitBreakBall();
            result.armorRemoved = before - state.armor;
        }
        return result;
    }

    inline int DirectDamage(int incoming, int defense, const State& state)
    {
        const int afterDefense = (std::max)(1, incoming - defense);
        // Defense -> quarter damage rounded UP -> minimum 1. Break bypasses Armor.
        return state.IsBroken() ? afterDefense : afterDefense / 4 + (afterDefense % 4 != 0);
    }
}
