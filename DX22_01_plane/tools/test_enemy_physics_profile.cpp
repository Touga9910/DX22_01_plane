#include "../StageDataLoader.h"
#include "../BallPhysicsRules.h"
#include "../BallTrajectoryPreview.h"
#include "../BossEncounterRules.h"
#include "test_checks.h"

#include <cmath>
#include <iostream>
#include <map>
#include <string>
#include <utility>

using DirectX::SimpleMath::Vector3;

namespace
{
bool Near(float first, float second, float tolerance = 0.00002f)
{
    return std::abs(first - second) <= tolerance;
}

float ExpectedMass(float radius)
{
    const double ratio = static_cast<double>(radius) / 2.4;
    return static_cast<float>(std::floor(ratio * ratio * ratio * 100.0 + 0.5001) / 100.0);
}

void AssertProfile(const BallStatus& status)
{
    assert(Near(status.friction, 0.02f));
    assert(Near(status.restitution, 0.8f));
    assert(Near(status.mass, ExpectedMass(status.radius)));
}

// Use the same impact and friction functions as the live world and forecast.
float ReceivingSpeed(const BallStatus& status)
{
    BallPhysicsRules::Body player, enemy;
    player.id = 1;
    player.player = true;
    player.status.radius = 2.4f;
    player.status.mass = 2.0f;
    player.position = Vector3(-player.status.radius - status.radius, 0, 0);
    player.velocity = Vector3(4, 0, 0);
    enemy.id = 2;
    enemy.enemy = true;
    enemy.status = status;
    assert(BallPhysicsRules::Pair(player, enemy));
    assert(enemy.velocity.x > 0 && Near(enemy.velocity.z, 0));
    return enemy.velocity.x;
}

void CheckPreviewContact(const BallStatus& status)
{
    BallTrajectoryPreview::BallSnapshot player, enemy;
    player.physics.id = 1;
    player.physics.player = true;
    player.physics.status.radius = 2.4f;
    player.physics.status.mass = 2.0f;
    player.physics.status.restitution = 0.8f;
    player.physics.status.friction = 0.02f;
    enemy.physics.id = 2;
    enemy.physics.enemy = true;
    enemy.physics.status = status;
    enemy.physics.position = Vector3(20, 0, 0);
    enemy.guideEnemy = true;
    BallTrajectoryPreview::Input input;
    input.playerId = 1;
    input.balls = {player, enemy};
    const auto result = BallTrajectoryPreview::Predict(input, Vector3(4, 0, 0), true);
    assert(result.complete && result.reachedBlockingContact);
    assert(result.initialContactGuide.hitEnemy);
    assert(Near(result.initialContactGuide.playerPosition.x,
        20.0f - player.physics.status.radius - status.radius, 0.0002f));
}
}

int main(int argc, char** argv)
{
    const std::string master = argc > 1 ? argv[1] : "assets/data/enemy_data.json";
    const std::string stageFile = argc > 2 ? argv[2] : "assets/data/stage_01.json";
    const auto enemies = StageDataLoader::LoadEnemyDefinitions(master);
    assert(!enemies.empty());
    std::map<std::string, BallStatus> byId;
    std::map<float, std::pair<float, float>> byRadius;
    int nuisanceProfiles = 0;
    for (const auto& enemy : enemies)
    {
        AssertProfile(enemy.status);
        byId.emplace(enemy.id, enemy.status);
        const float speed = ReceivingSpeed(enemy.status);
        const auto [at, inserted] = byRadius.emplace(enemy.status.radius,
            std::pair{enemy.status.mass, speed});
        if (!inserted)
        {
            assert(Near(at->second.first, enemy.status.mass));
            assert(Near(at->second.second, speed));
        }
        CheckPreviewContact(enemy.status);
        Vector3 velocity(3.2f, 0, 0), reference = velocity;
        float distance = 0, referenceDistance = 0;
        for (int tick = 0; tick < 512; ++tick)
        {
            BallPhysicsRules::EnemyFriction(velocity, enemy.status.friction);
            BallPhysicsRules::EnemyFriction(reference, 0.02f);
            distance += velocity.x;
            referenceDistance += reference.x;
            assert(Near(velocity.x, reference.x) && Near(distance, referenceDistance));
        }
        assert(velocity == Vector3::Zero);
        if (enemy.nuisanceBall.enabled)
        {
            BallStatus nuisance;
            nuisance.radius = enemy.nuisanceBall.radius;
            nuisance.mass = enemy.nuisanceBall.mass;
            nuisance.friction = enemy.nuisanceBall.friction;
            nuisance.restitution = enemy.nuisanceBall.restitution;
            AssertProfile(nuisance);
            ++nuisanceProfiles;
        }
    }
    float previousMass = 0, previousSpeed = 100;
    for (const auto& [radius, massAndSpeed] : byRadius)
    {
        assert(massAndSpeed.first > previousMass);
        assert(massAndSpeed.second < previousSpeed);
        previousMass = massAndSpeed.first;
        previousSpeed = massAndSpeed.second;
    }

    assert(Near(BossEncounterRules::MinionRadius, 2.4f));
    assert(Near(BossEncounterRules::MinionMass, byId.at(BossEncounterRules::MinionId).mass));
    const NuisanceBallData defaults;
    assert(Near(defaults.friction, 0.02f) && Near(defaults.mass, ExpectedMass(defaults.radius)));

    const auto stages = StageDataLoader::LoadAll(stageFile, master);
    assert(!stages.empty());
    int placedEnemies = 0;
    const auto checkSpawns = [&](const std::vector<EnemySpawnData>& spawns)
    {
        for (const auto& spawn : spawns)
        {
            const auto& expected = byId.at(spawn.enemyId);
            AssertProfile(spawn.enemyData.status);
            assert(Near(expected.mass, spawn.enemyData.status.mass));
            ++placedEnemies;
        }
    };
    for (const auto& stage : stages)
    {
        checkSpawns(stage.enemies);
        for (const auto& phase : stage.phaseBattle.phases) checkSpawns(phase.enemies);
    }
    assert(placedEnemies > 0);
    std::cout << "PASS: " << enemies.size() << " enemy masters, " << nuisanceProfiles
        << " nuisance profiles, " << placedEnemies << " authored spawns; equal-size impact, "
        << "size-based mass, shared friction and preview contact geometry\n";
}
