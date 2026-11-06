#pragma once

#include "BattleTypes.h"

#include <SimpleMath.h>

#include <algorithm>

// Pure per-enemy Charge interruption calculations.  This namespace has no
// knowledge of physics components, action budgets, UI, or concrete ball types.
namespace EnemyChargeRules
{
    inline int AccumulateAppliedDamage(
        int accumulatedDamage,
        EnemyDamageKind kind,
        int appliedDamage)
    {
        if (kind != EnemyDamageKind::PierceAxis ||
            appliedDamage <= 0)
        {
            return accumulatedDamage;
        }
        return accumulatedDamage + appliedDamage;
    }

    inline ChargeInterruptionResult EvaluateShot(
        bool active,
        bool heavyShot,
        const DirectX::SimpleMath::Vector3& startPosition,
        const DirectX::SimpleMath::Vector3& endPosition,
        float enemyRadius,
        int enemyMaxHp,
        float diameterMultiplier,
        float pierceMaxHpRatio,
        int pierceDamage)
    {
        ChargeInterruptionResult result;
        if (!active)
        {
            return result;
        }

        DirectX::SimpleMath::Vector3 displacement =
            endPosition - startPosition;
        displacement.y = 0.0f;
        result.movementDistance = displacement.Length();
        result.movementThreshold =
            (std::max)(0.0f, enemyRadius * 2.0f * diameterMultiplier);
        result.pierceDamage = pierceDamage;
        result.pierceDamageThreshold =
            (std::max)(0.0f,
                static_cast<float>((std::max)(0, enemyMaxHp)) *
                pierceMaxHpRatio);

        if (heavyShot &&
            result.movementDistance > 0.0f &&
            result.movementDistance >= result.movementThreshold)
        {
            result.reason = ChargeInterruptReason::Movement;
        }
        else if (result.pierceDamage > 0 &&
            static_cast<float>(result.pierceDamage) >=
            result.pierceDamageThreshold)
        {
            result.reason = ChargeInterruptReason::PierceDamage;
        }

        return result;
    }
}
