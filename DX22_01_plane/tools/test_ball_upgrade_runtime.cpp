#include "../PlayerBallSaveData.h"
#include "../BallMechanics.h"
#include "../BallStatusComponent.h"
#include "../StageDataLoader.h"
#include <cassert>
#include <iostream>

using nlohmann::json;
using PlayerBallSaveData::BallToJson;
using PlayerBallSaveData::BallFromJson;

int main()
{
    const auto loaded = PlayerBallDataLoader::Load("assets/data/player_status.json", {}, {});
    assert(loaded.defaultRunStatus.maxHp == 50 && loaded.defaultRunStatus.currentHp == 50);
    assert(loaded.ballDefinitions.size() == 5);
    PlayerRunStatus fallback;
    fallback.maxHp = 73;
    fallback.currentHp = 41;
    const auto missing = PlayerBallDataLoader::Load("tools/no_such_config.json", {}, fallback);
    assert(missing.defaultRunStatus.maxHp == 73 && missing.defaultRunStatus.currentHp == 41);

    BallStatusComponent runtime;
    runtime.SetMaxHp(50);
    runtime.SetCurrentHp(23);
    for (const auto& definition : loaded.ballDefinitions)
    {
        for (int level = 0; level <= PlayerBallData::MaxUpgradeLevel; ++level)
        {
            PlayerBallData ball = definition;
            ball.instanceId = 42;
            ball.upgradeLevel = level;
            ball.status = level == 0 ? definition.status : definition.upgradeTable[level - 1];
            runtime.ApplyStatusValuesOnly(ball.status);
            assert(runtime.GetMaxHp() == 50 && runtime.GetCurrentHp() == 23);
            const json saved = BallToJson(ball);
            assert(!saved.at("status").contains("max_hp") && !saved.at("status").contains("maxHp"));
            const PlayerBallData restored = BallFromJson(json::parse(saved.dump()));
            assert(BallToJson(restored) == saved);
            assert(restored.CanUpgrade() == (level < 2));

            json legacy = saved;
            legacy.erase("upgrade_model_version");
            legacy["status"]["max_hp"] = 999;
            legacy["status"]["attack"] = 99;
            for (auto& upgrade : legacy["upgrade_table"])
                upgrade = { { "attack", 99 }, { "defense", 99 } };
            const auto migrated = BallFromJson(legacy);
            assert(migrated.instanceId == 42 && migrated.upgradeLevel == level);
            assert(WriteBallStatus(migrated.status) == WriteBallStatus(ball.status));
            assert(WriteBallStatus(migrated.upgradeTable[1]) == WriteBallStatus(ball.upgradeTable[1]));
        }
    }
    auto corrupted = BallToJson(loaded.ballDefinitions.front());
    corrupted["instance_id"] = 1;
    for (const auto& mutation : { std::string("missing_status"), std::string("invalid_mass"), std::string("invalid_upgrade"), std::string("future_version") })
    {
        auto value = corrupted;
        if (mutation == "missing_status") value["status"].erase("mass");
        if (mutation == "invalid_mass") value["status"]["mass"] = -1;
        if (mutation == "invalid_upgrade") value["upgrade_table"][0]["pierceMaxUses"] = -1;
        if (mutation == "future_version") value["upgrade_model_version"] = 100;
        bool rejected = false;
        try { BallFromJson(value); } catch (...) { rejected = true; }
        assert(rejected);
    }

    const auto& standard = loaded.ballDefinitions[0];
    const auto& heavy = loaded.ballDefinitions[1];
    const auto& pierce = loaded.ballDefinitions[2];
    const auto& bounce = loaded.ballDefinitions[3];
    const auto& anchor = loaded.ballDefinitions[4];
    assert(standard.upgradeTable[1].attack > standard.status.attack);
    float lastEnemySpeed = 0;
    for (const BallStatus& status : { heavy.status, heavy.upgradeTable[0], heavy.upgradeTable[1] })
    {
        const auto hit = BallMechanics::ResolveNormalImpact(-5, 0, status.mass, 1, status.restitution, status.knockbackTransfer, 1, false, false);
        assert(-hit.second > lastEnemySpeed);
        const auto reversed = BallMechanics::ResolveNormalImpact(0, 5, 1, status.mass, status.restitution, 1, status.knockbackTransfer, false, false);
        assert(std::abs(hit.second + reversed.first) < 0.0001f);
        lastEnemySpeed = -hit.second;
        assert(status.attack == heavy.status.attack && status.defense == heavy.status.defense);
    }
    for (int level = 0; level < 3; ++level)
    {
        const auto& status = level == 0 ? pierce.status : pierce.upgradeTable[level - 1];
        assert(BallMechanics::PierceUses(status, false) == level + 1);
        assert(BallMechanics::PierceUses(status, true) == level + 2);
        assert(BallMechanics::PierceRetention(status, true) == 1.0f);
        if (level > 0) assert(status.pierceSpeedRetention > pierce.status.pierceSpeedRetention);
        assert(status.mass == pierce.status.mass && status.abilities.pierce);
    }
    assert(bounce.upgradeTable[1].restitution > bounce.status.restitution);
    assert(bounce.upgradeTable[1].friction < bounce.status.friction);
    assert(anchor.upgradeTable[1].anchorBrakeMultiplier > anchor.status.anchorBrakeMultiplier);
    assert(anchor.upgradeTable[1].anchorStopSpeedSquared > anchor.status.anchorStopSpeedSquared);
    assert(anchor.upgradeTable[0].anchorKnockbackImmune);
    const auto lockFirst = BallMechanics::ResolveNormalImpact(0, 5, 6, 1, 0.35f, 1, 1, true, false);
    const auto lockSecond = BallMechanics::ResolveNormalImpact(-5, 0, 1, 6, 0.35f, 1, 1, false, true);
    assert(lockFirst.first == 0 && lockFirst.second < 0);
    assert(lockSecond.second == 0 && lockSecond.first > 0);

    assert(BallMechanics::DirectionalDamage(6, 1.0f, 0.5f) == 3);
    assert(BallMechanics::DirectionalDamage(6, 0.0f, 0.5f) == 6);
    assert(BallMechanics::DirectionalDamage(6, -1.0f, 0.5f) == 6);
    assert(BallMechanics::DirectionalDamage(1, 1.0f, 0.5f) == 1);
    assert(BallMechanics::PocketDamage(18, 0.45f) == 9);
    assert(BallMechanics::PocketDamage(18, 0) == 0);
    const auto stages = StageDataLoader::LoadAll("assets/data/stage_01.json", "assets/data/enemy_data.json");
    assert(stages.size() == 8);
    int guarded = 0, pocket = 0;
    for (const auto& stage : stages)
    {
        for (const auto& spawn : stage.enemies)
        {
            if (spawn.enemyData.frontalDamageMultiplier < 1.0f)
            {
                assert(stage.stageType == StageType::MidBoss && spawn.enemyData.maxHp < 20);
                ++guarded;
            }
            if (spawn.enemyData.pocketDamageRatio > 0.0f)
            {
                assert(stage.stageType == StageType::MidBoss && spawn.enemyData.maxHp < 20);
                ++pocket;
            }
        }
    }
    assert(guarded == 1 && pocket == 1);
    std::cout << "PASS: HP independence, 15 save round trips, 15 legacy migrations, invalid saves, five upgrade axes, collision order, anchor lock, pierce/relic stacking, directional guard, pocket damage and stage loading.\n";
}
