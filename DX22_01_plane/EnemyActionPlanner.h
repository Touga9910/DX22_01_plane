#pragma once

#include "BattleTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct EnemyActionConfig
{
    int maximumBudget = 5;
    int normalAttackCost = 2;
    int chargeStartCost = 1;
    int chargeAttackCost = 3;
    int maximumChargeStartsPerTurn = 1;
    float chargeDamageMultiplier = 2.0f;
    float heavyInterruptDiameterMultiplier = 1.0f;
    float pierceInterruptMaxHpRatio = 0.25f;
};

struct EnemyActionCandidate
{
    std::uintptr_t id = 0;
    bool canAct = false;
    bool charging = false;
    bool chargeEnabled = false;
};

struct PlannedEnemyAction
{
    std::uintptr_t id = 0;
    EnemyActionType action = EnemyActionType::Wait;
    int cost = 0;
};

struct EnemyActionTurnPlan
{
    int budgetLimit = 0;
    int budgetSpent = 0;
    int reservedChargeAttackBudget = 0;
    std::vector<PlannedEnemyAction> actions;
};

// Owns only group-level action allocation: budget, priority and fair cursors.
// Execution, physics, damage and presentation remain outside this class.
class EnemyActionPlanner final
{
public:
    bool LoadConfig(const std::string& filePath);
    void Reset();

    EnemyActionTurnPlan PlanTurn(
        const std::vector<EnemyActionCandidate>& candidates);

    const EnemyActionConfig& Config() const { return m_Config; }
    void SetConfigForTesting(const EnemyActionConfig& config);

private:
    EnemyActionConfig m_Config{};
    std::size_t m_NormalCursor = 0;
    std::size_t m_ChargeStartCursor = 0;
    std::size_t m_ChargeAttackCursor = 0;
};
