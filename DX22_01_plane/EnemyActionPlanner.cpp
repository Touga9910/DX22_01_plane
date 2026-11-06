#include "EnemyActionPlanner.h"

#include "json/json.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <unordered_set>

namespace
{
    EnemyActionConfig Normalize(EnemyActionConfig config)
    {
        config.maximumBudget = (std::max)(0, config.maximumBudget);
        config.normalAttackCost = (std::max)(1, config.normalAttackCost);
        config.chargeStartCost = (std::max)(1, config.chargeStartCost);
        config.chargeAttackCost = (std::max)(1, config.chargeAttackCost);
        config.maximumChargeStartsPerTurn =
            (std::max)(0, config.maximumChargeStartsPerTurn);
        config.chargeDamageMultiplier =
            std::clamp(config.chargeDamageMultiplier, 1.0f, 100.0f);
        config.heavyInterruptDiameterMultiplier =
            std::clamp(config.heavyInterruptDiameterMultiplier, 0.0f, 100.0f);
        config.pierceInterruptMaxHpRatio =
            std::clamp(config.pierceInterruptMaxHpRatio, 0.0f, 1.0f);
        return config;
    }

    template<typename Predicate>
    std::vector<std::size_t> OrderedIndices(
        const std::vector<EnemyActionCandidate>& candidates,
        std::size_t cursor,
        Predicate predicate)
    {
        std::vector<std::size_t> result;
        if (candidates.empty())
        {
            return result;
        }
        const std::size_t start = cursor % candidates.size();
        for (std::size_t offset = 0; offset < candidates.size(); ++offset)
        {
            const std::size_t index = (start + offset) % candidates.size();
            if (predicate(candidates[index]))
            {
                result.push_back(index);
            }
        }
        return result;
    }
}

bool EnemyActionPlanner::LoadConfig(const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file)
    {
        std::cerr << "[EnemyActionPlanner] Config not found; defaults are used: "
            << filePath << std::endl;
        m_Config = Normalize({});
        return false;
    }

    try
    {
        nlohmann::json root;
        file >> root;
        EnemyActionConfig config{};
        const nlohmann::json budget = root.value(
            "attack_budget", nlohmann::json::object());
        config.maximumBudget = budget.value(
            "maximum_per_turn", config.maximumBudget);
        config.normalAttackCost = budget.value(
            "normal_attack_cost", config.normalAttackCost);
        config.chargeStartCost = budget.value(
            "charge_start_cost", config.chargeStartCost);
        config.chargeAttackCost = budget.value(
            "charge_attack_cost", config.chargeAttackCost);
        config.maximumChargeStartsPerTurn = budget.value(
            "maximum_charge_starts_per_turn",
            config.maximumChargeStartsPerTurn);

        const nlohmann::json charge = root.value(
            "charge", nlohmann::json::object());
        config.chargeDamageMultiplier = charge.value(
            "damage_multiplier", config.chargeDamageMultiplier);
        config.heavyInterruptDiameterMultiplier = charge.value(
            "heavy_interrupt_diameter_multiplier",
            config.heavyInterruptDiameterMultiplier);
        config.pierceInterruptMaxHpRatio = charge.value(
            "pierce_interrupt_max_hp_ratio",
            config.pierceInterruptMaxHpRatio);
        m_Config = Normalize(config);
        return true;
    }
    catch (const nlohmann::json::exception& error)
    {
        std::cerr << "[EnemyActionPlanner] Invalid config; defaults are used: "
            << error.what() << std::endl;
        m_Config = Normalize({});
        return false;
    }
}

void EnemyActionPlanner::Reset()
{
    m_NormalCursor = 0;
    m_ChargeStartCursor = 0;
    m_ChargeAttackCursor = 0;
}

void EnemyActionPlanner::SetConfigForTesting(
    const EnemyActionConfig& config)
{
    m_Config = Normalize(config);
    Reset();
}

EnemyActionTurnPlan EnemyActionPlanner::PlanTurn(
    const std::vector<EnemyActionCandidate>& candidates)
{
    EnemyActionTurnPlan plan;
    plan.budgetLimit = m_Config.maximumBudget;
    plan.actions.reserve(candidates.size());
    for (const EnemyActionCandidate& candidate : candidates)
    {
        plan.actions.push_back({ candidate.id, EnemyActionType::Wait, 0 });
    }

    int remaining = m_Config.maximumBudget;
    std::unordered_set<std::uintptr_t> assigned;

    // Prepared Charge attacks always receive budget first.  Under normal
    // operation there is at most one because starts are reservation-gated.
    const std::vector<std::size_t> charging = OrderedIndices(
        candidates,
        m_ChargeAttackCursor,
        [](const EnemyActionCandidate& candidate)
        {
            return candidate.canAct && candidate.charging;
        });
    for (const std::size_t index : charging)
    {
        if (remaining < m_Config.chargeAttackCost)
        {
            break;
        }
        plan.actions[index].action = EnemyActionType::ChargeAttack;
        plan.actions[index].cost = m_Config.chargeAttackCost;
        remaining -= m_Config.chargeAttackCost;
        assigned.insert(candidates[index].id);
        m_ChargeAttackCursor = (index + 1) % candidates.size();
    }

    const bool hasDeferredCharge = std::any_of(
        candidates.begin(), candidates.end(),
        [&assigned](const EnemyActionCandidate& candidate)
        {
            return candidate.charging &&
                !assigned.contains(candidate.id);
        });

    // Select and reserve Charge starts before filling normal slots so an
    // eligible Charge user cannot be permanently starved by cost-2 attacks.
    // Only the start cost is charged now; reservedChargeAttackBudget records
    // the next-turn commitment without consuming the current budget twice.
    std::vector<std::size_t> chargeStarts;
    if (!hasDeferredCharge &&
        m_Config.maximumChargeStartsPerTurn > 0 &&
        m_Config.chargeAttackCost <= m_Config.maximumBudget)
    {
        const std::vector<std::size_t> chargeCandidates = OrderedIndices(
            candidates,
            m_ChargeStartCursor,
            [&assigned](const EnemyActionCandidate& candidate)
            {
                return candidate.canAct &&
                    !candidate.charging &&
                    candidate.chargeEnabled &&
                    !assigned.contains(candidate.id);
            });
        for (const std::size_t index : chargeCandidates)
        {
            if (static_cast<int>(chargeStarts.size()) >=
                    m_Config.maximumChargeStartsPerTurn ||
                remaining < m_Config.chargeStartCost)
            {
                break;
            }
            chargeStarts.push_back(index);
            assigned.insert(candidates[index].id);
            remaining -= m_Config.chargeStartCost;
            m_ChargeStartCursor = (index + 1) % candidates.size();
        }
    }

    const std::vector<std::size_t> normalCandidates = OrderedIndices(
        candidates,
        m_NormalCursor,
        [&assigned](const EnemyActionCandidate& candidate)
        {
            return candidate.canAct &&
                !candidate.charging &&
                !assigned.contains(candidate.id);
        });
    for (const std::size_t index : normalCandidates)
    {
        if (remaining < m_Config.normalAttackCost)
        {
            break;
        }
        plan.actions[index].action = EnemyActionType::NormalAttack;
        plan.actions[index].cost = m_Config.normalAttackCost;
        remaining -= m_Config.normalAttackCost;
        assigned.insert(candidates[index].id);
        m_NormalCursor = (index + 1) % candidates.size();
    }

    for (const std::size_t index : chargeStarts)
    {
        plan.actions[index].action = EnemyActionType::ChargeStart;
        plan.actions[index].cost = m_Config.chargeStartCost;
    }

    plan.budgetSpent = m_Config.maximumBudget - remaining;
    plan.reservedChargeAttackBudget =
        static_cast<int>(chargeStarts.size()) *
        m_Config.chargeAttackCost;
    return plan;
}
