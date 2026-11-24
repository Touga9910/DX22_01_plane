#pragma once
#include "BossCombatRules.h"
#include <algorithm>
#include <vector>

// Pure rules: no Game, pointers, random state, logging or rendering.
namespace BossEncounterRules
{
    constexpr const char* MinionId = "enemy_boss_minion";
    constexpr int InitialMinions = 3, MaximumMinions = 4, SummonCount = 2;
    constexpr int MinionHp = 3, MinionAttack = 1, MaximumMinionAttackers = 2;
    constexpr float MinionRadius = 2.4f, MinionMass = 1.0f;
    struct SpawnPoint { float x, z; };
    // Distinct approach angles; no row of ammunition beside the core.
    constexpr SpawnPoint InitialMinionPositions[InitialMinions] = {{-32,-18},{32,-10},{-44,22}};
    constexpr SpawnPoint AdvanceMinionPositions[2] = {{-24,-8},{24,12}};
    enum class Intent { Normal, Prepare, Strong, Summon };
    // Escorts retain the encounter identity after a pocket destroys the core.
    // Resolve at shot end, never remove colliders in the middle of a chain.
    /// <summary>
    /// ボス戦が発生済みで、生存しているボス本体がいないかを判定する。
    /// </summary>
    constexpr bool EncounterCleared(bool hasCoreOrEscort, bool hasLivingCore)
    {
        return hasCoreOrEscort && !hasLivingCore;
    }
    struct TurnState
    {
        Intent intent = Intent::Normal;
        int turn = 0, nextMinionSerial = 0;
        bool interruptedThisShot = false;
        bool advanceSummonConsumed = false;
        int summonCycle = 0;
    };
    /// <summary>
    /// ボスの行動予定をログ用の文字列に変換する。
    /// </summary>
    inline const char* Name(Intent intent)
    {
        switch (intent) {
        case Intent::Normal: return "normal_attack";
        case Intent::Prepare: return "prepare_strong_attack";
        case Intent::Strong: return "strong_attack";
        default: return "summon_minions";
        }
    }
    /// <summary>
    /// ブレイクと中断の状態、および行動予定からボスの攻撃ダメージを求める。
    /// </summary>
    inline int BossDamage(const TurnState& turn, const BossCombatRules::State& armor)
    {
        if (armor.IsBroken() || turn.interruptedThisShot) return 0;
        return turn.intent == Intent::Normal ? 2 : turn.intent == Intent::Strong ? 4 : 0;
    }
    /// <summary>
    /// ショット終了時の中断状態を記録し、アーマーが回復したら次の予定を強攻撃にする。
    /// </summary>
    inline bool CompleteShot(TurnState& turn, BossCombatRules::State& armor)
    {
        turn.interruptedThisShot = armor.IsBroken();
        const bool restored = armor.EndShot();
        // The recovery shot remains interrupted. Advertise Strong for the next
        // player shot, which still has a full opportunity to Break or finish.
        if (restored) turn.intent = Intent::Strong;
        return restored;
    }
    /// <summary>
    /// ボスの敵ターンを進め、次の行動と追加するミニオン数を決める。
    /// </summary>
    inline int EndEnemyTurn(TurnState& turn, const BossCombatRules::State& armor, int livingMinions)
    {
        ++turn.turn;
        const bool interrupted = turn.interruptedThisShot;
        turn.interruptedThisShot = false;
        if (interrupted || armor.IsBroken())
        {
            // After the first full Break shot: supply is visible before the
            // last full shot. Re-Break never resets this cycle's entitlement.
            if (armor.IsBroken() && armor.shotsRemaining == 1 && !turn.advanceSummonConsumed)
            {
                turn.advanceSummonConsumed = true;
                return (std::max)(0, 2-livingMinions);
            }
            return 0;
        }
        const int spawn = turn.intent == Intent::Summon
            && !turn.advanceSummonConsumed
            ? (std::min)(SummonCount, (std::max)(0, MaximumMinions-livingMinions)) : 0;
        if (turn.intent == Intent::Summon)
        {
            turn.advanceSummonConsumed = false;
            ++turn.summonCycle;
        }
        turn.intent = static_cast<Intent>((static_cast<int>(turn.intent)+1)%4);
        return spawn;
    }
    /// <summary>
    /// 生存ミニオンの番号をそろえ、ターンに応じた順番で攻撃担当を選ぶ。
    /// </summary>
    inline std::vector<int> Attackers(std::vector<int> livingSerials, int turn)
    {
        std::sort(livingSerials.begin(), livingSerials.end());
        std::vector<int> selected;
        if (livingSerials.empty()) return selected;
        for (int i=0;i<(std::min)(MaximumMinionAttackers, static_cast<int>(livingSerials.size()));++i)
            selected.push_back(livingSerials[(turn+i)%livingSerials.size()]);
        return selected;
    }
    /// <summary>
    /// ミニオンからボスへの衝突で発生するアーマー変化とダメージを求める。
    /// </summary>
    inline BossCombatRules::PocketResult MinionImpact(BossCombatRules::State& armor, int hp)
    {
        if (hp<=0) return {};
        BossCombatRules::PocketResult result;
        result.applied = true;
        const int before = armor.armor;
        result.breakStarted = armor.HitBreakBall();
        result.armorRemoved = before-armor.armor;
        result.damage = (std::min)(hp, BossCombatRules::BreakBallDamage);
        return result;
    }
    /// <summary>
    /// ボスとミニオンの攻撃を合計し、シールドを引いた被ダメージを返す。
    /// </summary>
    inline int IncomingDamage(const TurnState& turn, const BossCombatRules::State& armor,
                              bool bossAlive, int assignedMinionDamage, int shield, bool bossAttackAssigned = true)
    {
        return bossAlive ? (std::max)(0, (bossAttackAssigned ? BossDamage(turn,armor) : 0)+assignedMinionDamage-shield) : 0;
    }
}
