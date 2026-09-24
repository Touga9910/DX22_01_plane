#pragma once

#include <algorithm>

// Value-only combat state shared by live play and disposable shot predictions.
namespace BossCombatRules
{
    constexpr int MaxArmor = 2;
    constexpr int BreakShots = 2;
    constexpr int BreakBallDamage = 4;
    struct State
    {
        int armor = MaxArmor;
        int shotsRemaining = 0;
        bool startedThisShot = false;
        bool IsBroken() const { return shotsRemaining > 0; }
        void BeginShot() { startedThisShot = false; }
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

    inline int DirectDamage(int incoming, int defense, const State& state)
    {
        const int afterDefense = (std::max)(1, incoming - defense);
        // Defense -> quarter damage rounded UP -> minimum 1. Break bypasses Armor.
        return state.IsBroken() ? afterDefense : afterDefense / 4 + (afterDefense % 4 != 0);
    }
}
