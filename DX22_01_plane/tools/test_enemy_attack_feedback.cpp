#include "../EnemyAttackTiming.h"
#include "../CombatAudioRules.h"
#include <cassert>
#include <limits>
#include <iostream>
/// <summary>
/// 敵攻撃が着弾するまでの時間進行と、対応する効果音の規則を検証する。
/// </summary>
int main()
{
    double delay = EnemyAttackTiming::FlightSeconds;
    assert(EnemyAttackTiming::Advance(delay, 0) == delay); // pause
    assert(EnemyAttackTiming::Advance(delay, -1) == delay);
    assert(EnemyAttackTiming::Advance(delay, std::numeric_limits<double>::quiet_NaN()) == delay);
    delay = EnemyAttackTiming::Advance(delay, .09);
    assert(delay > 0); // HP must remain unchanged during flight
    delay = EnemyAttackTiming::Advance(delay, .09);
    assert(delay == 0);
    assert(EnemyAttackTiming::GapSeconds > 0);
    assert(CombatAudioRules::CategoryId(CombatSeCategory::PlayerHurt) !=
        CombatAudioRules::CategoryId(CombatSeCategory::ShieldBlock));
    assert(CombatAudioRules::Get(CombatSeCategory::PlayerHurt).maximumVoices > 0);
    std::cout << "Enemy attack timing and audio rules passed\n";
}
