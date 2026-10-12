#include "../PlayerBallSaveData.h"
#include "../BallMechanics.h"
#include "../BallPhysicsRules.h"
#include "../BallStatusComponent.h"
#include "../CushionChargeRules.h"
#include "../StageDataLoader.h"
#include "../EnemyGimmickRules.h"
#include "../PocketReturnRules.h"
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
	assert(loaded.ballDefinitions.size() == 18);
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
			assert(saved.at("upgrade_model_version") ==
				PlayerBallSaveData::UpgradeModelVersion);
            assert(!saved.at("status").contains("max_hp") && !saved.at("status").contains("maxHp"));
			assert(!saved.at("status").contains("defense"));
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
	const auto& traceDriver = loaded.ballDefinitions[9];
	const auto& pierceFinisher = loaded.ballDefinitions[10];
	const auto& ricochetFinisher = loaded.ballDefinitions[11];
	const auto& anchorFinisher = loaded.ballDefinitions[12];
	const auto& collisionBoost = loaded.ballDefinitions[13];
	const auto& lightweight = loaded.ballDefinitions[14];
	const auto& minionFinisher = loaded.ballDefinitions[15];
	const auto& reflectionSetup = loaded.ballDefinitions[16];
	const auto& originAnchor = loaded.ballDefinitions[17];
	assert(standard.category == BallCategory::Standard);
	assert(heavy.category == BallCategory::Heavy);
	assert(pierce.category == BallCategory::Pierce);
	assert(bounce.category == BallCategory::Bounce);
	assert(anchor.category == BallCategory::Anchor);
	assert(cushion.category == BallCategory::Bounce);
	assert(chainImpact.category == BallCategory::Heavy);
	assert(refractivePierce.category == BallCategory::Pierce);
	assert(stopShield.category == BallCategory::Anchor);
	assert(traceDriver.category == BallCategory::Pierce);
	assert(pierceFinisher.category == BallCategory::Pierce);
	assert(ricochetFinisher.category == BallCategory::Bounce);
	assert(anchorFinisher.category == BallCategory::Anchor);
	assert(chainImpact.status.chainImpactRadius == 12.0f);
	assert(chainImpact.upgradeTable[1].chainImpactRadius == 18.0f);
	assert(chainImpact.status.heavyCollisionConsumeAmount == 5);
	assert(refractivePierce.status.abilities.pierce &&
		refractivePierce.status.abilities.refractAfterPierce);
	assert(refractivePierce.upgradeTable[1].pierceMaxUses == 3);
	assert(stopShield.status.stopShieldAmount == 3);
	assert(stopShield.upgradeTable[0].stopShieldAmount == 5);
	assert(stopShield.upgradeTable[1].stopShieldAmount == 7);
	assert(!traceDriver.status.abilities.pierce && traceDriver.status.pierceStackConsumeLimit == 3);
	assert(pierceFinisher.status.pierceFinisherBaseBonus == 1);
	assert(ricochetFinisher.status.pinballCollisionsPerStage == 3);
	assert(ricochetFinisher.upgradeTable[0].pinballCollisionsPerStage == 3);
	assert(ricochetFinisher.upgradeTable[1].pinballCollisionsPerStage == 2);
	assert(anchorFinisher.status.anchorFinisherAoeThreshold == 5);
	assert(collisionBoost.status.attack == 0 &&
		collisionBoost.status.heavySetupStackConsume == 3);
	assert(lightweight.status.abilities.lightweight &&
		lightweight.upgradeTable[0].lightweightSpeedLossReduction == 0.5f &&
		lightweight.upgradeTable[1].lightweightSpeedLossReduction == 0.5f &&
		lightweight.upgradeTable[1].lightweightKnockbackMultiplier == 2.0f);
	assert(minionFinisher.status.pierceMaxUses == 3);
	assert(reflectionSetup.upgradeTable[1].reflectionSetupAmount == 3);
	assert(originAnchor.status.knockbackTransfer == 0.0f &&
		originAnchor.status.abilities.returnToShotStart);
    assert(standard.upgradeTable[1].attack > standard.status.attack);
	assert(heavy.status.attack == 2 && heavy.upgradeTable[0].attack == 3 &&
		heavy.upgradeTable[1].attack == 4);
	assert(pierce.status.attack == 1 && pierce.upgradeTable[0].attack == 2 &&
		pierce.upgradeTable[1].attack == 3);
	assert(refractivePierce.status.attack == 1 &&
		refractivePierce.upgradeTable[0].attack == 2 &&
		refractivePierce.upgradeTable[1].attack == 3);
    float lastEnemySpeed = 0;
    for (const BallStatus& status : { heavy.status, heavy.upgradeTable[0], heavy.upgradeTable[1] })
    {
        const auto hit = BallMechanics::ResolveNormalImpact(-5, 0, status.mass, 1, status.restitution, status.knockbackTransfer, 1, false, false);
        assert(-hit.second > lastEnemySpeed);
        const auto reversed = BallMechanics::ResolveNormalImpact(0, 5, 1, status.mass, status.restitution, 1, status.knockbackTransfer, false, false);
        assert(std::abs(hit.second + reversed.first) < 0.0001f);
        lastEnemySpeed = -hit.second;
		assert(status.defense == 0);
    }
    for (int level = 0; level < 3; ++level)
    {
        const auto& status = level == 0 ? pierce.status : pierce.upgradeTable[level - 1];
        assert(BallMechanics::PierceUses(status, false) == 3);
        assert(BallMechanics::PierceUses(status, true) == 4);
        assert(BallMechanics::PierceRetention(status, true) == 1.0f);
        if (level > 0) assert(status.pierceSpeedRetention > pierce.status.pierceSpeedRetention);
        assert(status.mass == pierce.status.mass && status.abilities.pierce);
    }
    assert(bounce.upgradeTable[1].restitution == bounce.status.restitution);
    assert(bounce.upgradeTable[1].friction == bounce.status.friction);
    assert(anchor.upgradeTable[1].anchorBrakeMultiplier == anchor.status.anchorBrakeMultiplier);
    assert(anchor.upgradeTable[1].anchorStopSpeedSquared == anchor.status.anchorStopSpeedSquared);
    assert(!anchor.upgradeTable[0].anchorKnockbackImmune);
	BallPhysicsRules::Body normalLightweight;
	normalLightweight.id = 1;
	normalLightweight.player = true;
	normalLightweight.position = Vector3(0.0f, 0.0f, 0.0f);
	normalLightweight.velocity = Vector3(10.0f, 0.0f, 0.0f);
	normalLightweight.status = lightweight.status;
	BallPhysicsRules::Body reducedLightweight = normalLightweight;
	reducedLightweight.id = 2;
	reducedLightweight.status = lightweight.upgradeTable[0];
	BallPhysicsRules::Body normalEnemy;
	normalEnemy.id = 3;
	normalEnemy.enemy = true;
	normalEnemy.position = Vector3(4.0f, 0.0f, 0.0f);
	normalEnemy.status = BallStatus{};
	normalEnemy.status.radius = 2.4f;
	BallPhysicsRules::Body reducedEnemy = normalEnemy;
	reducedEnemy.id = 4;
	assert(BallPhysicsRules::Pair(normalLightweight, normalEnemy));
	assert(BallPhysicsRules::Pair(reducedLightweight, reducedEnemy));
	const float normalSpeed = normalLightweight.velocity.Length();
	const float reducedSpeed = reducedLightweight.velocity.Length();
	assert(std::abs(reducedSpeed -
		(10.0f - (10.0f - normalSpeed) * 0.5f)) < 0.001f);
    const auto lockFirst = BallMechanics::ResolveNormalImpact(0, 5, 6, 1, 0.35f, 1, 1, true, false);
    const auto lockSecond = BallMechanics::ResolveNormalImpact(-5, 0, 1, 6, 0.35f, 1, 1, false, true);
    assert(lockFirst.first == 0 && lockFirst.second < 0);
    assert(lockSecond.second == 0 && lockSecond.first > 0);

	BallStatusComponent shieldRuntime;
	shieldRuntime.SetMaxHp(20);
	shieldRuntime.SetCurrentHp(20);
	assert(!shieldRuntime.ApplyFinalDamage(3));
	assert(shieldRuntime.GetCurrentHp() == 17);

	CushionChargeRules::State cushionState{};
	Vector3 reflectedVelocity(4.0f, 0.0f, -3.0f);
	CushionChargeRules::BeginPlayerShot(cushionState);
	CushionChargeRules::ReflectionConfig generation;
	generation.generatesStacks = true;
	generation.generationLimit = 3;
	generation.stacksPerReflection = 1;
	CushionChargeRules::ApplyValidReflection(cushionState, generation, reflectedVelocity);
	CushionChargeRules::ApplyValidReflection(cushionState, generation, reflectedVelocity);
	assert(cushionState.stackCount == 2);
	CushionChargeRules::ReflectionConfig use;
	use.consumesStacks = true;
	use.consumptionLimit = 1;
	use.reflectedSpeedMultiplier = 1.2f;
	auto used = CushionChargeRules::ApplyValidReflection(
		cushionState, use, reflectedVelocity);
	assert(used.consumed == 1 && cushionState.stackCount == 1);
	assert(std::abs(reflectedVelocity.Length() - 6.0f) < 0.0001f);
	CushionChargeRules::EndPlayerShot(cushionState);
	assert(CushionChargeRules::TotalStacks(cushionState) == 1);
	assert(cushion.status.cushionReflectionLimit == 3);
	assert(cushion.upgradeTable[0].cushionReflectionLimit == 6);
	assert(cushion.upgradeTable[1].cushionStackGenerateAmount == 2);

    assert(BallMechanics::DirectionalDamage(6, 1.0f, 0.5f) == 3);
    assert(BallMechanics::DirectionalDamage(6, 0.0f, 0.5f) == 6);
    assert(BallMechanics::DirectionalDamage(6, -1.0f, 0.5f) == 6);
    assert(BallMechanics::DirectionalDamage(1, 1.0f, 0.5f) == 1);
    assert(BallMechanics::PocketDamage(18, 0.45f) == 9);
    assert(BallMechanics::PocketDamage(18, 0) == 0);
    const auto topLeftPocket = PocketReturnRules::ClosestPocketCenter(
        Vector3(-70.0f, 1.0f, 34.0f));
    assert(topLeftPocket.x < 0.0f && topLeftPocket.z > 0.0f);
    const auto topLeftReturn = PocketReturnRules::ReturnAnchor(
        Vector3(-70.0f, 1.0f, 34.0f), 10.0f);
    assert(topLeftReturn.x > topLeftPocket.x && topLeftReturn.z < topLeftPocket.z);
    const auto bottomSideReturn = PocketReturnRules::ReturnAnchor(
        Vector3(1.0f, 1.0f, -35.0f), 10.0f);
    assert(std::abs(bottomSideReturn.x) < 0.0001f);
    assert(std::abs(bottomSideReturn.z + 26.0f) < 0.0001f);
    const std::vector<float> collisionMultipliers = {3.0f, 2.0f, 1.0f, 0.75f, 0.5f};
    assert(EnemyGimmickRules::ScaleCollisionDamage(4,
        EnemyGimmickRules::CollisionDamageMultiplier(collisionMultipliers, 0)) == 12);
    assert(EnemyGimmickRules::ScaleCollisionDamage(4,
        EnemyGimmickRules::CollisionDamageMultiplier(collisionMultipliers, 1)) == 8);
    assert(EnemyGimmickRules::ScaleCollisionDamage(4,
        EnemyGimmickRules::CollisionDamageMultiplier(collisionMultipliers, 99)) == 2);
    const auto enemyDefinitions = StageDataLoader::LoadEnemyDefinitions("assets/data/enemy_data.json");
    const auto nuisanceDefinition = std::find_if(enemyDefinitions.begin(), enemyDefinitions.end(),
        [](const EnemyData& data) { return data.id == "enemy_nuisance_spawner"; });
    const auto shellDefinition = std::find_if(enemyDefinitions.begin(), enemyDefinitions.end(),
        [](const EnemyData& data) { return data.id == "enemy_collision_shell"; });
    assert(nuisanceDefinition != enemyDefinitions.end());
    assert(nuisanceDefinition->nuisanceBall.enabled);
    assert(nuisanceDefinition->nuisanceBall.debuffs.GetMagnitude(StatusEffectType::AttackDown) == 1);
    assert(shellDefinition != enemyDefinitions.end());
    assert(shellDefinition->collisionDamageMultipliers == collisionMultipliers);
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
	std::cout << "PASS: HP independence, 18 ball definitions, four synergy resources, save round trips, collision order, pierce/refraction, directional guard, pocket damage and stage loading.\n";
}
