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
        /// <summary>
        /// ブレイク状態の残りショットがあるかを判定する。
        /// </summary>
        bool IsBroken() const { return shotsRemaining > 0; }
        /// <summary>
        /// ショット開始時にアーマーの状態変化とポケット効果の適用記録を初期化する。
        /// </summary>
        void BeginShot() { startedThisShot = false; pocketResolvedThisShot = false; }
        /// <summary>
        /// ブレイクボールとの接触でアーマーを減らし、尽きたらブレイクを開始する。
        /// </summary>
        bool HitBreakBall()
        {
            if (IsBroken()) return false; // Fixed damage still applies; no duration refresh.
            armor = (std::max)(0, armor - 1);
            if (armor != 0) return false;
            shotsRemaining = BreakShots;
            startedThisShot = true;
            return true;
        }
        /// <summary>
        /// ショット終了時にブレイクの残り回数を更新し、期限に達したらアーマーを回復する。
        /// </summary>
        bool EndShot()
        {
            if (!IsBroken() || startedThisShot) return false;
            if (--shotsRemaining != 0) return false;
            armor = MaxArmor;
            return true;
        }
    };

    /// <summary>
    /// ボスの最大HPからブレイク中のポケットダメージを計算する。
    /// </summary>
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

    /// <summary>
    /// ポケット効果をショットごとに一度適用し、ブレイク状態に応じてダメージやアーマーを処理する。
    /// </summary>
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

    /// <summary>
    /// 防御力を適用したボスへの直接ダメージを、アーマー状態に応じて補正する。
    /// </summary>
    inline int DirectDamage(int incoming, int defense, const State& state)
    {
        const int afterDefense = (std::max)(1, incoming - defense);
        // Defense -> quarter damage rounded UP -> minimum 1. Break bypasses Armor.
        return state.IsBroken() ? afterDefense : afterDefense / 4 + (afterDefense % 4 != 0);
    }
}
