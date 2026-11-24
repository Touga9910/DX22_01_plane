#include "../BossEncounterRules.h"
#include "../BossPocketRules.h"
#include <cassert>
#include <iostream>
/// <summary>
/// ボス戦の行動順、召喚上限、装甲破壊、衝突とポケットの規則を検証する。
/// </summary>
int main()
{
    using namespace BossEncounterRules;
    assert(!EncounterCleared(false,false)); // ordinary battle / empty loading board
    assert(!EncounterCleared(true,true));   // live core: keep escorts
    assert(EncounterCleared(true,false));   // dead core or core removed by pocket
    using DirectX::SimpleMath::Vector3;
    std::vector<BossPocketRules::Obstacle> occupied = {{{0,1,0},3},{{0,1,20},6}};
    const std::vector<Collision::Sphere> pockets = {};
    for (const auto& slot : InitialMinionPositions)
    {
        const Vector3 desired(slot.x,TableConfig::FIELD_HEIGHT,slot.z);
        const auto placed = BossPocketRules::FindSpawnPosition(desired,MinionRadius,occupied,pockets);
        assert(placed && *placed==desired); // never reinterpret as a pocket entry
        occupied.push_back({*placed,MinionRadius});
    }
    const Vector3 blocked(-32,1,-18);
    const auto fallback = BossPocketRules::FindSpawnPosition(blocked,MinionRadius,occupied,pockets);
    assert(fallback && *fallback!=blocked && BossPocketRules::IsSafePosition(*fallback,MinionRadius,occupied,pockets));
    assert(!BossPocketRules::IsSafePosition({72,1,0},MinionRadius,{},pockets));
    assert(!BossPocketRules::IsSafePosition({0,1,35},MinionRadius,{},pockets));
    BossCombatRules::State armor;
    TurnState turn;
    assert(InitialMinions==3 && MaximumMinions==4 && BossCombatRules::MaxArmor==2);
    assert(BossDamage(turn,armor)==2);
    assert(IncomingDamage(turn,armor,true,2,3)==1);
    assert(IncomingDamage(turn,armor,false,2,0)==0);
    assert(IncomingDamage(turn,armor,true,2,0,false)==2);
    assert((Attackers({9,3,7},0)==std::vector<int>{3,7}));
    assert((Attackers({9,3,7},1)==std::vector<int>{7,9}));
    assert(Attackers({},0).empty());
    armor.BeginShot();
    auto first=MinionImpact(armor,60); assert(first.damage==4 && armor.armor==1 && !first.breakStarted);
    auto second=MinionImpact(armor,56); assert(second.damage==4 && armor.armor==0 && second.breakStarted);
    assert(BossDamage(turn,armor)==0);
    assert(!CompleteShot(turn,armor) && armor.shotsRemaining==2);
    assert(EndEnemyTurn(turn,armor,1)==0);
    armor.BeginShot();
    assert(MinionImpact(armor,1).damage==1 && armor.shotsRemaining==2);
    assert(!CompleteShot(turn,armor) && armor.shotsRemaining==1);
    assert(EndEnemyTurn(turn,armor,1)==1 && turn.advanceSummonConsumed);
    armor.BeginShot();
    assert(CompleteShot(turn,armor) && armor.armor==2 && armor.shotsRemaining==0);
    assert(BossDamage(turn,armor)==0); // last Break shot still cancels that enemy phase
    EndEnemyTurn(turn,armor,1);
    assert(BossDamage(turn,armor)==4 && turn.intent==Intent::Strong);
    assert(IncomingDamage(turn,armor,true,2,3)==3);
    EndEnemyTurn(turn,armor,3); assert(turn.intent==Intent::Summon && BossDamage(turn,armor)==0);
    assert(EndEnemyTurn(turn,armor,3)==0 && turn.intent==Intent::Normal && !turn.advanceSummonConsumed);
    assert(EndEnemyTurn(turn,armor,3)==0 && turn.intent==Intent::Prepare);
    assert(BossDamage(turn,armor)==0);
    EndEnemyTurn(turn,armor,3); assert(turn.intent==Intent::Strong);
    turn.intent=Intent::Summon; assert(EndEnemyTurn(turn,armor,4)==0);
    turn.intent=Intent::Summon; assert(EndEnemyTurn(turn,armor,0)==2);
    turn.intent=Intent::Strong; armor.BeginShot();MinionImpact(armor,60);MinionImpact(armor,56);
    CompleteShot(turn,armor); assert(BossDamage(turn,armor)==0);
    assert(EndEnemyTurn(turn,armor,0)==0); // interruption cannot summon
    for (int living=0;living<=4;++living)
    {
        BossCombatRules::State a;
        TurnState t;
        a.BeginShot();a.HitBreakBall();a.HitBreakBall();CompleteShot(t,a);
        assert(a.shotsRemaining==2 && EndEnemyTurn(t,a,living)==0);
        a.BeginShot();CompleteShot(t,a);
        assert(a.shotsRemaining==1);
        assert(EndEnemyTurn(t,a,living)==(std::max)(0,2-living));
        assert(t.advanceSummonConsumed && t.summonCycle==0);
        // Even consuming every supplied ball and starting a new Break cannot
        // reclaim the prepaid summon before the Summon phase has completed.
        a.BeginShot();CompleteShot(t,a);EndEnemyTurn(t,a,0);
        a.BeginShot();a.HitBreakBall();a.HitBreakBall();CompleteShot(t,a);
        assert(EndEnemyTurn(t,a,0)==0);
        a.BeginShot();CompleteShot(t,a);assert(a.shotsRemaining==1);
        assert(EndEnemyTurn(t,a,0)==0 && t.advanceSummonConsumed);
        a.BeginShot();CompleteShot(t,a);assert(EndEnemyTurn(t,a,0)==0);
        assert(t.intent==Intent::Strong);
        EndEnemyTurn(t,a,0);assert(t.intent==Intent::Summon);
        assert(EndEnemyTurn(t,a,0)==0 && !t.advanceSummonConsumed && t.summonCycle==1);
        a.BeginShot();a.HitBreakBall();a.HitBreakBall();CompleteShot(t,a);EndEnemyTurn(t,a,0);
        a.BeginShot();CompleteShot(t,a);assert(EndEnemyTurn(t,a,0)==2);
    }
    BossCombatRules::State pocket;
    pocket.BeginShot();auto hit=BossCombatRules::ResolvePocket(pocket,60,60);
    assert(hit.armorRemoved==1 && hit.damage==0);
    assert(!BossCombatRules::ResolvePocket(pocket,60,60).applied);
    pocket.BeginShot();BossCombatRules::ResolvePocket(pocket,60,60);
    assert(pocket.IsBroken());pocket.BeginShot();
    assert(BossCombatRules::ResolvePocket(pocket,60,60).damage==15);
    std::cout << "Boss encounter rules: intent cycle, capped summons, fixed attack slots, Break timing, impacts and pockets passed\n";
}
