#include "../EnemyActionPlanner.h"
#include "../EnemyChargeRules.h"

#include <SimpleMath.h>

#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <unordered_set>

namespace
{
    int Count(
        const EnemyActionTurnPlan& plan,
        EnemyActionType action)
    {
        int count = 0;
        for (const PlannedEnemyAction& assignment : plan.actions)
        {
            if (assignment.action == action)
            {
                ++count;
            }
        }
        return count;
    }

    std::unordered_set<std::uintptr_t> ActorsWith(
        const EnemyActionTurnPlan& plan,
        EnemyActionType action)
    {
        std::unordered_set<std::uintptr_t> result;
        for (const PlannedEnemyAction& assignment : plan.actions)
        {
            if (assignment.action == action)
            {
                result.insert(assignment.id);
            }
        }
        return result;
    }
}

int main()
{
    EnemyActionPlanner planner;
    const std::filesystem::path configPath =
        std::filesystem::path(__FILE__).parent_path().parent_path() /
        "assets/data/enemy_action_rules.json";
    assert(planner.LoadConfig(configPath.string()));
    assert(planner.Config().maximumBudget == 5);
    assert(planner.Config().normalAttackCost == 2);
    assert(planner.Config().chargeStartCost == 1);
    assert(planner.Config().chargeAttackCost == 3);
    assert(std::abs(planner.Config().chargeDamageMultiplier - 2.0f) < 0.0001f);
    assert(std::abs(planner.Config().heavyInterruptDiameterMultiplier - 1.0f) < 0.0001f);
    assert(std::abs(planner.Config().pierceInterruptMaxHpRatio - 0.25f) < 0.0001f);

    std::vector<EnemyActionCandidate> enemies;
    for (std::uintptr_t id = 1; id <= 8; ++id)
    {
        enemies.push_back({ id, true, false, id == 1 });
    }

    const EnemyActionTurnPlan first = planner.PlanTurn(enemies);
    assert(first.budgetLimit == 5);
    assert(first.budgetSpent == 5);
    assert(first.reservedChargeAttackBudget == 3);
    assert(Count(first, EnemyActionType::NormalAttack) == 2);
    assert(Count(first, EnemyActionType::ChargeStart) == 1);
    assert(Count(first, EnemyActionType::Wait) == 5);
    assert(first.actions[0].action == EnemyActionType::ChargeStart);

    // The committed plan is a value.  Later defeat/interruption cannot cause
    // another candidate to inherit its already-spent budget.
    enemies[1].canAct = false;
    assert(first.actions[1].action == EnemyActionType::NormalAttack);

    enemies[0].charging = true;
    enemies[1].canAct = true;
    const EnemyActionTurnPlan second = planner.PlanTurn(enemies);
    assert(second.budgetSpent == 5);
    assert(second.reservedChargeAttackBudget == 0);
    assert(Count(second, EnemyActionType::ChargeAttack) == 1);
    assert(Count(second, EnemyActionType::NormalAttack) == 1);
    assert(second.actions[0].action == EnemyActionType::ChargeAttack);

    // After the Charge resolves, the next turn gets a fresh budget and the
    // normal round-robin continues after previously selected actors.
    enemies[0].charging = false;
    const EnemyActionTurnPlan third = planner.PlanTurn(enemies);
    assert(third.budgetSpent == 5);
    assert(third.actions[0].action == EnemyActionType::ChargeStart);
    const auto firstNormals = ActorsWith(first, EnemyActionType::NormalAttack);
    const auto thirdNormals = ActorsWith(third, EnemyActionType::NormalAttack);
    for (const std::uintptr_t id : thirdNormals)
    {
        assert(!firstNormals.contains(id));
    }

    // Even if external/debug state creates multiple Charging enemies, the
    // planner never exceeds the budget and does not start another Charge while
    // a prepared attack had to wait.
    planner.Reset();
    enemies[0].charging = true;
    enemies[1].charging = true;
    const EnemyActionTurnPlan crowdedCharge = planner.PlanTurn(enemies);
    assert(crowdedCharge.budgetSpent <= crowdedCharge.budgetLimit);
    assert(Count(crowdedCharge, EnemyActionType::ChargeAttack) == 1);
    assert(Count(crowdedCharge, EnemyActionType::ChargeStart) == 0);

    using DirectX::SimpleMath::Vector3;
    auto below = EnemyChargeRules::EvaluateShot(
        true, true, Vector3::Zero,
        Vector3(3.99f, 0.0f, 0.0f), 2.0f, 20, 1.0f, 0.25f, 0);
    assert(!below.WasInterrupted());

    auto atDiameter = EnemyChargeRules::EvaluateShot(
        true, true, Vector3::Zero,
        Vector3(4.0f, 50.0f, 0.0f), 2.0f, 20, 1.0f, 0.25f, 0);
    assert(atDiameter.reason == ChargeInterruptReason::Movement);

    auto nonHeavy = EnemyChargeRules::EvaluateShot(
        true, false, Vector3::Zero,
        Vector3(20.0f, 0.0f, 0.0f), 2.0f, 20, 1.0f, 0.25f, 0);
    assert(!nonHeavy.WasInterrupted());

    int pierceDamage = 0;
    pierceDamage = EnemyChargeRules::AccumulateAppliedDamage(
        pierceDamage, EnemyDamageKind::Other, 100);
    pierceDamage = EnemyChargeRules::AccumulateAppliedDamage(
        pierceDamage, EnemyDamageKind::PierceAxis, 2);
    pierceDamage = EnemyChargeRules::AccumulateAppliedDamage(
        pierceDamage, EnemyDamageKind::PierceAxis, 3);
    auto pierce = EnemyChargeRules::EvaluateShot(
        true, false, Vector3::Zero, Vector3::Zero,
        2.0f, 20, 1.0f, 0.25f, pierceDamage);
    assert(pierce.reason == ChargeInterruptReason::PierceDamage);
    assert(pierce.pierceDamage == 5);
    assert(std::abs(pierce.pierceDamageThreshold - 5.0f) < 0.0001f);

    // Inactive shots never inherit a previous accumulator.
    auto reset = EnemyChargeRules::EvaluateShot(
        false, false, Vector3::Zero, Vector3::Zero,
        2.0f, 20, 1.0f, 0.25f, pierceDamage);
    assert(!reset.WasInterrupted() && reset.pierceDamage == 0);

    std::cout << "Enemy action budget, Charge reservation, fairness and interruption rules: passed\n";
    return 0;
}
