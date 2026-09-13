#include "../PlayerBallSaveData.h"
#include "../BallMechanics.h"
#include "../BallStatusComponent.h"
#include "../CushionChargeRules.h"
#include "../StageDataLoader.h"
#include <cassert>
#include <iostream>

using nlohmann::json;
using PlayerBallSaveData::BallToJson;
using PlayerBallSaveData::BallFromJson;
using DirectX::SimpleMath::Vector3;

int main()
{
    const auto loaded = PlayerBallDataLoader::Load("assets/data/player_status.json", {}, {});
    assert(loaded.defaultRunStatus.maxHp == 50 && loaded.defaultRunStatus.currentHp == 50);
	assert(loaded.ballDefinitions.size() == 9);
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
			json categoryLegacy = saved;
			categoryLegacy.erase("category");
			assert(BallFromJson(categoryLegacy).category == ball.category);
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
    for (const auto& mutation : { std::string("missing_status"), std::string("invalid_mass"), std::string("invalid_upgrade"), std::string("invalid_category"), std::string("future_version") })
    {
        auto value = corrupted;
        if (mutation == "missing_status") value["status"].erase("mass");
        if (mutation == "invalid_mass") value["status"]["mass"] = -1;
        if (mutation == "invalid_upgrade") value["upgrade_table"][0]["pierceMaxUses"] = -1;
		if (mutation == "invalid_category") value["category"] = "unknown";
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
	const auto& cushion = loaded.ballDefinitions[5];
	const auto& chainImpact = loaded.ballDefinitions[6];
	const auto& refractivePierce = loaded.ballDefinitions[7];
	const auto& stopShield = loaded.ballDefinitions[8];
	assert(standard.category == BallCategory::Standard);
	assert(heavy.category == BallCategory::Heavy);
	assert(pierce.category == BallCategory::Pierce);
	assert(bounce.category == BallCategory::Bounce);
	assert(anchor.category == BallCategory::Anchor);
	assert(cushion.category == BallCategory::Bounce);
	assert(chainImpact.category == BallCategory::Heavy);
	assert(refractivePierce.category == BallCategory::Pierce);
	assert(stopShield.category == BallCategory::Anchor);
	assert(chainImpact.status.chainImpactRadius == 12.0f);
	assert(chainImpact.upgradeTable[1].chainImpactRadius == 20.0f);
	assert(refractivePierce.status.abilities.pierce &&
		refractivePierce.status.abilities.refractAfterPierce);
	assert(refractivePierce.upgradeTable[1].pierceMaxUses == 3);
	assert(stopShield.status.stopShieldAmount == 3);
	assert(stopShield.upgradeTable[0].stopShieldAmount == 5);
	assert(stopShield.upgradeTable[1].stopShieldAmount == 7);
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

	BallStatusComponent shieldRuntime;
	shieldRuntime.SetMaxHp(20);
	shieldRuntime.SetCurrentHp(20);
	assert(!shieldRuntime.ApplyFinalDamage(3));
	assert(shieldRuntime.GetCurrentHp() == 17);

	const float halfWidth = TableConfig::GetFieldWidth() * 0.5f;
	const float halfDepth = TableConfig::GetFieldDepth() * 0.5f;
	const Collision::Segment topWall{
		{-halfWidth, 0, halfDepth}, {halfWidth, 0, halfDepth} };
	const Collision::Segment leftWall{
		{-halfWidth, 0, -halfDepth}, {-halfWidth, 0, halfDepth} };
	for (int part = 0; part < 4; ++part)
	{
		const float x = -halfWidth + (static_cast<float>(part) + 0.5f) *
			(halfWidth * 2.0f / 4.0f);
		assert(CushionChargeRules::RegionFromContact(
			topWall, { x, 0, halfDepth }) == part);
	}
	assert(CushionChargeRules::RegionFromContact(
		leftWall, { -halfWidth, 0, -halfDepth * 0.5f }) == 8);
	assert(CushionChargeRules::RegionFromContact(
		leftWall, { -halfWidth, 0, halfDepth * 0.5f }) == 9);
	CushionChargeRules::State cushionState{};
	bool cushionBoostConsumed = false;
	Vector3 reflectedVelocity(4.0f, 0.0f, -3.0f);
	assert(!CushionChargeRules::ApplyPlayerWallContact(
		cushionState, 2, cushion.status.cushionChargeSpeedMultiplier,
		cushionBoostConsumed, reflectedVelocity));
	assert(cushionState[2].active && !cushionState[2].usableThisShot &&
		reflectedVelocity.Length() == 5.0f);
	assert(!CushionChargeRules::ApplyPlayerWallContact(
		cushionState, 2, cushion.upgradeTable[0].cushionChargeSpeedMultiplier,
		cushionBoostConsumed, reflectedVelocity));
	assert(cushionState[2].active && !cushionState[2].usableThisShot);
	CushionChargeRules::BeginPlayerShot(cushionState);
	assert(cushionState[2].usableThisShot);
	assert(CushionChargeRules::ApplyPlayerWallContact(
		cushionState, 2, 1.0f, cushionBoostConsumed, reflectedVelocity));
	assert(!cushionState[2].active);
	assert(std::abs(reflectedVelocity.Length() - 6.0f) < 0.0001f);

	// One shot can consume several regions, but its speed boost is applied once.
	cushionState[3] = { true, false, 1.2f };
	cushionState[4] = { true, false, 1.3f };
	CushionChargeRules::BeginPlayerShot(cushionState);
	cushionBoostConsumed = false;
	reflectedVelocity = Vector3(3.0f, 0.0f, 4.0f);
	assert(CushionChargeRules::ApplyPlayerWallContact(
		cushionState, 3, 1.0f, cushionBoostConsumed, reflectedVelocity));
	assert(std::abs(reflectedVelocity.Length() - 6.0f) < 0.0001f);
	assert(!CushionChargeRules::ApplyPlayerWallContact(
		cushionState, 4, 1.0f, cushionBoostConsumed, reflectedVelocity));
	assert(!cushionState[3].active && !cushionState[4].active);
	assert(std::abs(reflectedVelocity.Length() - 6.0f) < 0.0001f);

	// Untouched old charges expire, while charges refreshed this shot survive.
	cushionState[5] = { true, false, 1.2f };
	cushionState[6] = { true, false, 1.2f };
	CushionChargeRules::BeginPlayerShot(cushionState);
	assert(!CushionChargeRules::ApplyPlayerWallContact(
		cushionState, 6, cushion.upgradeTable[1].cushionChargeSpeedMultiplier,
		cushionBoostConsumed, reflectedVelocity));
	CushionChargeRules::EndPlayerShot(cushionState);
	assert(!cushionState[5].active);
	assert(cushionState[6].active && !cushionState[6].usableThisShot);
	assert(CushionChargeRules::PendingNextShotCount(cushionState) == 1);
	assert(cushion.status.cushionChargeSpeedMultiplier == 1.1f);
	assert(cushion.upgradeTable[0].cushionChargeSpeedMultiplier == 1.2f);
	assert(cushion.upgradeTable[1].cushionChargeSpeedMultiplier == 1.3f);

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
	std::cout << "PASS: HP independence, 27 save round trips, five categories, three variant abilities, cushion lifetime, legacy migrations, invalid saves, collision order, anchor lock, pierce/refraction, directional guard, pocket damage and stage loading.\n";
}
