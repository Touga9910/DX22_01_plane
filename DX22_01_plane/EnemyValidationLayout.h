#pragma once

#include "EnemyValidationSettings.h"
#include "StageData.h"

#include <string>
#include <vector>

struct EnemyValidationLayoutResult
{
    std::vector<EnemySpawnData> enemies;
    std::string error;

    bool Succeeded() const { return error.empty(); }
};

// Builds only the one-battle validation composition and initial positions.
// It does not apply HP scaling or mutate live physics objects.
namespace EnemyValidationLayout
{
    EnemyValidationLayoutResult Generate(
        const std::vector<EnemyData>& catalog,
        const EnemyValidationSettings& settings,
        const DirectX::SimpleMath::Vector3& playerPosition,
        float playerRadius);
}
