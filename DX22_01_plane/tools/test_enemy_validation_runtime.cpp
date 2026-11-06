#include "EnemyHpScaling.h"
#include "EnemyValidationLayout.h"
#include "TableConfig.h"
#include "TableGeometry.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <tuple>
#include <vector>

using DirectX::SimpleMath::Vector3;

namespace
{
    EnemyData MakeEnemy(const char* id, int hp, float radius)
    {
        EnemyData enemy;
        enemy.id = id;
        enemy.maxHp = hp;
        enemy.status.radius = radius;
        enemy.scale = {radius, radius, radius};
        return enemy;
    }

    void AssertSafe(
        const std::vector<EnemySpawnData>& enemies,
        const Vector3& player,
        float playerRadius)
    {
        const float halfWidth = TableConfig::GetFieldWidth() * 0.5f;
        const float halfDepth = TableConfig::GetFieldDepth() * 0.5f;
        for (std::size_t index = 0; index < enemies.size(); ++index)
        {
            const auto& enemy = enemies[index];
            const float radius = enemy.enemyData.status.radius;
            assert(std::abs(enemy.position.x) + radius < halfWidth);
            assert(std::abs(enemy.position.z) + radius < halfDepth);
            assert(std::hypot(
                enemy.position.x - player.x,
                enemy.position.z - player.z) > radius + playerRadius);
            for (const auto& pocket : TableConfig::GetPocketCenters())
            {
                assert(std::hypot(
                    enemy.position.x - pocket.x,
                    enemy.position.z - pocket.z) >
                    radius + TableConfig::POCKET_RADIUS);
            }
            for (std::size_t other = 0; other < index; ++other)
            {
                assert(std::hypot(
                    enemy.position.x - enemies[other].position.x,
                    enemy.position.z - enemies[other].position.z) >
                    radius + enemies[other].enemyData.status.radius);
            }
        }
    }

    int CountId(
        const std::vector<EnemySpawnData>& enemies,
        const std::string& id)
    {
        int count = 0;
        for (const auto& enemy : enemies)
            if (enemy.enemyId == id) ++count;
        return count;
    }
}

int main()
{
    assert(EnemyHpScaling::ApplyMultiplier(3, 1.0f) == 3);
    assert(EnemyHpScaling::ApplyMultiplier(3, 1.5f) == 5);
    assert(EnemyHpScaling::ApplyMultiplier(20, 2.0f) == 40);
    assert(EnemyHpScaling::ApplyMultiplier(
        (std::numeric_limits<int>::max)(), 2.0f) ==
        (std::numeric_limits<int>::max)());

    const std::vector<EnemyData> catalog = {
        MakeEnemy("enemy_normal", 3, 2.4f),
        MakeEnemy("enemy_strong", 5, 2.6f),
        MakeEnemy("enemy_tank", 20, 2.8f),
    };
    const Vector3 player(0.0f, TableConfig::FIELD_HEIGHT, 0.0f);
    EnemyValidationSettings settings;
    settings.enabled = true;
    settings.seed = 12345;

    for (const auto [count, low, standard] : {
        std::tuple{6, 2, 3}, std::tuple{8, 3, 4}, std::tuple{10, 4, 5}})
    {
        settings.enemyCount = count;
        for (const auto pattern : {
            EnemyValidationLayoutPattern::Dense,
            EnemyValidationLayoutPattern::Line,
            EnemyValidationLayoutPattern::Random})
        {
            settings.layoutPattern = pattern;
            const auto result = EnemyValidationLayout::Generate(
                catalog, settings, player, 2.4f);
            assert(result.Succeeded());
            assert(static_cast<int>(result.enemies.size()) == count);
            assert(CountId(result.enemies, "enemy_normal") == low);
            assert(CountId(result.enemies, "enemy_strong") == standard);
            assert(CountId(result.enemies, "enemy_tank") == 1);
            AssertSafe(result.enemies, player, 2.4f);
        }
    }

    settings.enemyCount = 10;
    settings.layoutPattern = EnemyValidationLayoutPattern::Random;
    const auto first = EnemyValidationLayout::Generate(
        catalog, settings, player, 2.4f);
    const auto second = EnemyValidationLayout::Generate(
        catalog, settings, player, 2.4f);
    assert(first.Succeeded() && second.Succeeded());
    for (std::size_t index = 0; index < first.enemies.size(); ++index)
    {
        assert(first.enemies[index].position == second.enemies[index].position);
    }

    std::cout << "PASS: enemy HP multiplier, 6/8/10 compositions, "
        "Dense/Line/Random safe layouts, deterministic random seed\n";
}
