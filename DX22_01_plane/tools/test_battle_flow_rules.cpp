#include "../BattleFlowRules.h"

#include <cassert>
#include <iostream>

/// <summary>
/// 停止確認、フェーズ開始と進行、勝利と戦闘後の復帰に関する規則を検証する。
/// </summary>
int main()
{
    using namespace BattleFlowRules;

    assert(!HasConfirmedStop(0));
    assert(!HasConfirmedStop(RequiredStoppedTicks - 1));
    assert(HasConfirmedStop(RequiredStoppedTicks));
    assert(HasConfirmedStop(RequiredStoppedTicks + 1));

    const PhaseStartState legacy = BeginPhaseBattle(false, 3, 2);
    assert(legacy.currentPhase == 0);
    assert(legacy.totalPhases == 0);
    assert(legacy.turnsUntilNextPhase == 0);

    const PhaseStartState empty = BeginPhaseBattle(true, 0, 2);
    assert(empty.currentPhase == 0);
    assert(empty.totalPhases == 0);
    assert(empty.turnsUntilNextPhase == 0);

    const PhaseStartState single = BeginPhaseBattle(true, 1, 2);
    assert(single.currentPhase == 1);
    assert(single.totalPhases == 1);
    assert(single.turnsUntilNextPhase == 0);

    const PhaseStartState multiple = BeginPhaseBattle(true, 3, 2);
    assert(multiple.currentPhase == 1);
    assert(multiple.totalPhases == 3);
    assert(multiple.turnsUntilNextPhase == 2);

    assert(!CanAdvancePhase(false, 1, 3, true, 0));
    assert(!CanAdvancePhase(true, 3, 3, true, 0));
    assert(!CanAdvancePhase(true, 1, 3, false, 1));
    assert(CanAdvancePhase(true, 1, 3, false, 0));
    assert(CanAdvancePhase(true, 1, 3, true, 2));

    assert(!IsPhaseVictory(false, 1, 1, 0));
    assert(!IsPhaseVictory(true, 0, 0, 0));
    assert(!IsPhaseVictory(true, 1, 2, 0));
    assert(!IsPhaseVictory(true, 2, 2, 1));
    assert(IsPhaseVictory(true, 2, 2, 0));

    assert(!CanRecoverClearedBattle(
        false, false, BattleState::AimingDirection, true));
    assert(!CanRecoverClearedBattle(
        true, true, BattleState::Finished, true));
    assert(!CanRecoverClearedBattle(
        true, false, BattleState::BallsMoving, true));
    assert(!CanRecoverClearedBattle(
        true, false, BattleState::AimingDirection, false));
    assert(CanRecoverClearedBattle(
        true, false, BattleState::AimingDirection, true));
    assert(CanRecoverClearedBattle(
        true, false, BattleState::TurnEnd, true));

    std::cout <<
        "Battle flow: stop confirmation, phase start/advance/victory, "
        "and clear recovery rules passed\n";
}
