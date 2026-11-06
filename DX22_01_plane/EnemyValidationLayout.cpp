#include "EnemyValidationLayout.h"

#include "TableConfig.h"
#include "TableGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <unordered_map>

using DirectX::SimpleMath::Vector3;

namespace
{
    constexpr float kWallClearance = 0.5f;
    constexpr float kBallClearance = 1.0f;
    constexpr float kDenseGap = 1.5f;
    constexpr float kLineGap = 1.5f;

    float DistanceXZ(const Vector3& left, const Vector3& right)
    {
        return std::hypot(left.x - right.x, left.z - right.z);
    }

    bool CanPlace(
        const EnemySpawnData& candidate,
        const Vector3& playerPosition,
        float playerRadius,
        const std::vector<EnemySpawnData>& placed)
    {
        const float radius = candidate.enemyData.status.radius;
        const float halfWidth = TableConfig::GetFieldWidth() * 0.5f;
        const float halfDepth = TableConfig::GetFieldDepth() * 0.5f;
        if (!std::isfinite(candidate.position.x) ||
            !std::isfinite(candidate.position.z) ||
            radius <= 0.0f || !std::isfinite(radius) ||
            std::abs(candidate.position.x) + radius + kWallClearance >= halfWidth ||
            std::abs(candidate.position.z) + radius + kWallClearance >= halfDepth)
        {
            return false;
        }

        if (DistanceXZ(candidate.position, playerPosition) <=
            radius + (std::max)(0.01f, playerRadius) + kBallClearance)
        {
            return false;
        }

        for (const Vector3& pocket : TableConfig::GetPocketCenters())
        {
            if (DistanceXZ(candidate.position, pocket) <=
                radius + TableConfig::POCKET_RADIUS + kWallClearance)
            {
                return false;
            }
        }

        for (const EnemySpawnData& other : placed)
        {
            if (DistanceXZ(candidate.position, other.position) <=
                radius + other.enemyData.status.radius + kBallClearance)
            {
                return false;
            }
        }
        return true;
    }

    bool CommitPositions(
        std::vector<EnemySpawnData>& enemies,
        const std::vector<Vector3>& positions,
        const Vector3& playerPosition,
        float playerRadius)
    {
        if (enemies.size() != positions.size()) return false;
        std::vector<EnemySpawnData> placed;
        placed.reserve(enemies.size());
        for (std::size_t index = 0; index < enemies.size(); ++index)
        {
            EnemySpawnData candidate = enemies[index];
            candidate.position = positions[index];
            candidate.position.y = TableConfig::FIELD_HEIGHT;
            candidate.enemyData.initPosition = candidate.position;
            if (!CanPlace(candidate, playerPosition, playerRadius, placed))
            {
                return false;
            }
            placed.push_back(std::move(candidate));
        }
        enemies = std::move(placed);
        return true;
    }

    bool ArrangeDense(
        std::vector<EnemySpawnData>& enemies,
        const Vector3& playerPosition,
        float playerRadius)
    {
        float maxRadius = 0.01f;
        for (const EnemySpawnData& enemy : enemies)
        {
            maxRadius = (std::max)(maxRadius, enemy.enemyData.status.radius);
        }
        const float spacing = maxRadius * 2.0f + kDenseGap;
        const std::size_t columns = 4;
        const std::size_t rowCount =
            (enemies.size() + columns - 1) / columns;

        const std::array<Vector3, 8> anchors = {
            Vector3(0.0f, TableConfig::FIELD_HEIGHT, -21.0f),
            Vector3(0.0f, TableConfig::FIELD_HEIGHT, 21.0f),
            Vector3(-30.0f, TableConfig::FIELD_HEIGHT, -14.0f),
            Vector3(30.0f, TableConfig::FIELD_HEIGHT, -14.0f),
            Vector3(-30.0f, TableConfig::FIELD_HEIGHT, 14.0f),
            Vector3(30.0f, TableConfig::FIELD_HEIGHT, 14.0f),
            Vector3(-38.0f, TableConfig::FIELD_HEIGHT, 0.0f),
            Vector3(38.0f, TableConfig::FIELD_HEIGHT, 0.0f),
        };
        std::array<std::size_t, anchors.size()> order{};
        for (std::size_t index = 0; index < order.size(); ++index) order[index] = index;
        std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right)
        {
            return DistanceXZ(anchors[left], playerPosition) >
                DistanceXZ(anchors[right], playerPosition);
        });

        for (const std::size_t anchorIndex : order)
        {
            std::vector<Vector3> positions;
            positions.reserve(enemies.size());
            std::size_t enemyIndex = 0;
            for (std::size_t row = 0; row < rowCount; ++row)
            {
                const std::size_t inRow = (std::min)(
                    columns, enemies.size() - enemyIndex);
                const float startX =
                    -static_cast<float>(inRow - 1) * spacing * 0.5f;
                const float zOffset =
                    (static_cast<float>(row) -
                     static_cast<float>(rowCount - 1) * 0.5f) * spacing;
                for (std::size_t column = 0; column < inRow;
                    ++column, ++enemyIndex)
                {
                    positions.emplace_back(
                        anchors[anchorIndex].x + startX +
                            static_cast<float>(column) * spacing,
                        TableConfig::FIELD_HEIGHT,
                        anchors[anchorIndex].z + zOffset);
                }
            }
            if (CommitPositions(
                enemies, positions, playerPosition, playerRadius))
            {
                return true;
            }
        }
        return false;
    }

    bool ArrangeLine(
        std::vector<EnemySpawnData>& enemies,
        const Vector3& playerPosition,
        float playerRadius)
    {
        const float halfWidth = TableConfig::GetFieldWidth() * 0.5f;
        const std::array<float, 2> directions =
            halfWidth - playerPosition.x >= halfWidth + playerPosition.x
            ? std::array<float, 2>{1.0f, -1.0f}
            : std::array<float, 2>{-1.0f, 1.0f};

        for (const float direction : directions)
        {
            std::vector<Vector3> positions;
            positions.reserve(enemies.size());
            float distance = (std::max)(0.01f, playerRadius) +
                enemies.front().enemyData.status.radius + 2.0f;
            for (std::size_t index = 0; index < enemies.size(); ++index)
            {
                if (index > 0)
                {
                    distance += enemies[index - 1].enemyData.status.radius +
                        enemies[index].enemyData.status.radius + kLineGap;
                }
                positions.emplace_back(
                    playerPosition.x + direction * distance,
                    TableConfig::FIELD_HEIGHT,
                    playerPosition.z);
            }
            if (CommitPositions(
                enemies, positions, playerPosition, playerRadius))
            {
                return true;
            }
        }
        return false;
    }

    bool ArrangeRandom(
        std::vector<EnemySpawnData>& enemies,
        const EnemyValidationSettings& settings,
        const Vector3& playerPosition,
        float playerRadius)
    {
        std::mt19937 random(settings.seed);
        const float halfWidth = TableConfig::GetFieldWidth() * 0.5f;
        const float halfDepth = TableConfig::GetFieldDepth() * 0.5f;
        std::vector<EnemySpawnData> placed;
        placed.reserve(enemies.size());

        for (const EnemySpawnData& enemy : enemies)
        {
            const float radius = enemy.enemyData.status.radius;
            const float xLimit = halfWidth - radius - kWallClearance;
            const float zLimit = halfDepth - radius - kWallClearance;
            std::uniform_real_distribution<float> xDistribution(-xLimit, xLimit);
            std::uniform_real_distribution<float> zDistribution(-zLimit, zLimit);
            bool found = false;
            for (int attempt = 0; attempt < 2048; ++attempt)
            {
                EnemySpawnData candidate = enemy;
                candidate.position = {
                    xDistribution(random),
                    TableConfig::FIELD_HEIGHT,
                    zDistribution(random),
                };
                candidate.enemyData.initPosition = candidate.position;
                if (!CanPlace(candidate, playerPosition, playerRadius, placed))
                {
                    continue;
                }
                placed.push_back(std::move(candidate));
                found = true;
                break;
            }
            if (!found) return false;
        }
        enemies = std::move(placed);
        return true;
    }
}

EnemyValidationLayoutResult EnemyValidationLayout::Generate(
    const std::vector<EnemyData>& catalog,
    const EnemyValidationSettings& settings,
    const Vector3& playerPosition,
    float playerRadius)
{
    EnemyValidationLayoutResult result;
    if (!IsSupportedEnemyValidationCount(settings.enemyCount))
    {
        result.error = "Enemy validation count must be 6, 8, or 10.";
        return result;
    }

    std::unordered_map<std::string, const EnemyData*> definitions;
    for (const EnemyData& enemy : catalog) definitions.emplace(enemy.id, &enemy);
    for (const char* required : {"enemy_normal", "enemy_strong", "enemy_tank"})
    {
        if (!definitions.contains(required))
        {
            result.error = std::string("Missing enemy definition for validation: ") + required;
            return result;
        }
    }

    int lowCount = 0;
    int standardCount = 0;
    if (settings.enemyCount == 6) { lowCount = 2; standardCount = 3; }
    else if (settings.enemyCount == 8) { lowCount = 3; standardCount = 4; }
    else { lowCount = 4; standardCount = 5; }

    const auto append = [&](const char* id, int count)
    {
        for (int index = 0; index < count; ++index)
        {
            EnemySpawnData spawn;
            spawn.enemyId = id;
            spawn.enemyData = *definitions.at(id);
            result.enemies.push_back(std::move(spawn));
        }
    };
    append("enemy_normal", lowCount);
    append("enemy_strong", standardCount);
    append("enemy_tank", 1);

    bool arranged = false;
    switch (settings.layoutPattern)
    {
    case EnemyValidationLayoutPattern::Dense:
        arranged = ArrangeDense(result.enemies, playerPosition, playerRadius);
        break;
    case EnemyValidationLayoutPattern::Line:
        arranged = ArrangeLine(result.enemies, playerPosition, playerRadius);
        break;
    case EnemyValidationLayoutPattern::Random:
        arranged = ArrangeRandom(
            result.enemies, settings, playerPosition, playerRadius);
        break;
    }

    if (!arranged)
    {
        result.enemies.clear();
        result.error = "The selected enemy count and layout cannot be placed safely with the current player position and radius.";
    }
    return result;
}
