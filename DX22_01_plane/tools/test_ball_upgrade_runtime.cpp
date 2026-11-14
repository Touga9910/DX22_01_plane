#include "../PlayerBallSaveData.h"
#include "../BallMechanics.h"
#include "../BallPhysicsRules.h"
#include "../BallStatusComponent.h"
#include "../CushionChargeRules.h"
#include "../StageDataLoader.h"
#include "../StageSelector.h"
#include "../GameEncounterBalance.h"
#include "../EnemyGimmickRules.h"
#include "../PocketReturnRules.h"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

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
			const json currentMcpStatus = WritePlayerBallStatus(ball.status);
			assert(!currentMcpStatus.contains("defense"));
			assert(currentMcpStatus.at("attack") == ball.status.attack);
			assert(currentMcpStatus.at("heavyFinisherDamagePerCollision") ==
				ball.status.heavyFinisherDamagePerCollision);
			assert(currentMcpStatus.at("cushionBounceAttackBonus") ==
				ball.status.cushionBounceAttackBonus);
			assert(currentMcpStatus.at("anchorEnemyStackGenerate") ==
				ball.status.anchorEnemyStackGenerate);
			assert(WritePlayerBallStatus(
				ReadPlayerBallStatus(currentMcpStatus)) == currentMcpStatus);
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
	assert(heavy.status.attack == 2 && heavy.upgradeTable[0].attack == 2 &&
		heavy.upgradeTable[1].attack == 3);
	assert(pierce.status.attack == 1 && pierce.upgradeTable[0].attack == 1 &&
		pierce.upgradeTable[1].attack == 2);
	assert(refractivePierce.status.attack == 1 &&
		refractivePierce.upgradeTable[0].attack == 1 &&
		refractivePierce.upgradeTable[1].attack == 2);
	for (const PlayerBallData* generator : {
		&heavy, &pierce, &refractivePierce, &pierceFinisher,
		&lightweight, &minionFinisher })
	{
		assert(generator->upgradeTable[0].attack == generator->status.attack);
		assert(generator->upgradeTable[1].attack == generator->status.attack + 1);
		assert(generator->status.eventStackGenerationBonus == 0);
		assert(generator->upgradeTable[0].eventStackGenerationBonus == 1);
		assert(generator->upgradeTable[1].eventStackGenerationBonus == 2);
	}
	for (const PlayerBallData* generator : {
		&anchor, &cushion, &reflectionSetup, &originAnchor })
	{
		assert(generator->upgradeTable[0].attack == generator->status.attack);
		assert(generator->upgradeTable[1].attack == generator->status.attack + 1);
	}
	assert(anchor.status.anchorEnemyStackGenerate == 2 &&
		anchor.upgradeTable[0].anchorEnemyStackGenerate == 3 &&
		anchor.upgradeTable[1].anchorEnemyStackGenerate == 4);
	assert(cushion.status.cushionStackGenerateAmount == 1 &&
		cushion.upgradeTable[0].cushionStackGenerateAmount == 2 &&
		cushion.upgradeTable[1].cushionStackGenerateAmount == 3);
	assert(reflectionSetup.status.reflectionSetupAmount == 1 &&
		reflectionSetup.upgradeTable[0].reflectionSetupAmount == 2 &&
		reflectionSetup.upgradeTable[1].reflectionSetupAmount == 3);
	assert(originAnchor.status.anchorEnemyStackGenerate == 1 &&
		originAnchor.upgradeTable[0].anchorEnemyStackGenerate == 2 &&
		originAnchor.upgradeTable[1].anchorEnemyStackGenerate == 3);
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
	assert(cushion.upgradeTable[1].cushionStackGenerateAmount == 3);

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
	const auto findEnemy = [&enemyDefinitions](const char* id)
	{
		return std::find_if(
			enemyDefinitions.begin(),
			enemyDefinitions.end(),
			[id](const EnemyData& enemy) { return enemy.id == id; });
	};
	const auto swarm = findEnemy("enemy_swarm_amplifier");
	const auto brittle = findEnemy("enemy_impact_brittle");
	const auto resonator = findEnemy("enemy_protection_resonator");
	assert(swarm != enemyDefinitions.end());
	assert(brittle != enemyDefinitions.end());
	assert(resonator != enemyDefinitions.end());
	assert(swarm->boardAbility.type == EnemyBoardAbilityType::SwarmAmplifier);
	assert(std::abs(swarm->boardAbility.radius - 12.5f) < 0.001f);
	assert(brittle->boardAbility.type == EnemyBoardAbilityType::ImpactBrittle);
	assert(std::abs(brittle->boardAbility.brittleCollisionSpeed - 6.0f) < 0.001f);
	assert(resonator->boardAbility.type == EnemyBoardAbilityType::ProtectionResonator);
	assert(resonator->boardAbility.protectionDamageReduction == 1);
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
    assert(stages.size() == 19);
    std::unordered_set<std::string> stageIds;
    for (const auto& stage : stages)
    {
        assert(stageIds.insert(stage.id).second);
    }
    for (const char* existingId : {
        "normal_001", "normal_002", "normal_003", "normal_004",
        "midboss_001", "midboss_002", "boss_001", "boss_002" })
    {
        assert(stageIds.contains(existingId));
    }

    struct BoardStageExpectation
    {
        const char* id;
        int difficulty;
        std::size_t enemyCount;
        std::unordered_map<std::string, int> composition;
    };
    const std::array<BoardStageExpectation, 6> boardStageExpectations{{
        { "normal_swarm_center", 2, 6,
            {{"enemy_swarm_amplifier", 1}, {"enemy_normal", 2},
             {"enemy_strong", 2}, {"enemy_tank", 1}} },
        { "normal_brittle_wall", 2, 6,
            {{"enemy_impact_brittle", 1}, {"enemy_normal", 2},
             {"enemy_strong", 2}, {"enemy_tank", 1}} },
        { "normal_resonance_guard", 2, 8,
            {{"enemy_protection_resonator", 1}, {"enemy_normal", 3},
             {"enemy_strong", 3}, {"enemy_tank", 1}} },
        { "normal_dual_threat", 2, 8,
            {{"enemy_swarm_amplifier", 1}, {"enemy_impact_brittle", 1},
             {"enemy_normal", 2}, {"enemy_strong", 3},
             {"enemy_tank", 1}} },
        { "normal_resonant_swarm", 3, 10,
            {{"enemy_swarm_amplifier", 1},
             {"enemy_protection_resonator", 1}, {"enemy_normal", 3},
             {"enemy_strong", 4}, {"enemy_tank", 1}} },
        { "normal_triple_encirclement", 3, 10,
            {{"enemy_swarm_amplifier", 1}, {"enemy_impact_brittle", 1},
             {"enemy_protection_resonator", 1}, {"enemy_normal", 3},
             {"enemy_strong", 3}, {"enemy_tank", 1}} },
    }};
    std::vector<StageData> boardStages;
    const float fieldHalfWidth = TableConfig::GetFieldWidth() * 0.5f;
    const float fieldHalfDepth = TableConfig::GetFieldDepth() * 0.5f;
    const float playerRadius = 2.4f;
    const std::array<Vector3, 6> pockets{{
        {-fieldHalfWidth, 1.0f, -fieldHalfDepth},
        {0.0f, 1.0f, -fieldHalfDepth},
        {fieldHalfWidth, 1.0f, -fieldHalfDepth},
        {-fieldHalfWidth, 1.0f, fieldHalfDepth},
        {0.0f, 1.0f, fieldHalfDepth},
        {fieldHalfWidth, 1.0f, fieldHalfDepth},
    }};
    const auto planarDistance = [](const Vector3& left, const Vector3& right)
    {
        const float x = left.x - right.x;
        const float z = left.z - right.z;
        return std::sqrt(x * x + z * z);
    };
    constexpr float pocketSafetyMargin = 6.0f;
    for (const StageData& stage : stages)
    {
        for (std::size_t index = 0; index < stage.enemies.size(); ++index)
        {
            const auto& spawn = stage.enemies[index];
            const float radius = spawn.enemyData.status.radius;
            assert(std::abs(spawn.position.x) + radius < fieldHalfWidth);
            assert(std::abs(spawn.position.z) + radius < fieldHalfDepth);
            assert(planarDistance(spawn.position, Vector3::Zero) >
                radius + playerRadius);
            for (const Vector3& pocketPosition : pockets)
            {
                assert(planarDistance(spawn.position, pocketPosition) >
                    radius + TableConfig::POCKET_RADIUS + pocketSafetyMargin);
            }
            for (std::size_t other = 0; other < index; ++other)
            {
                const auto& otherSpawn = stage.enemies[other];
                assert(planarDistance(spawn.position, otherSpawn.position) >
                    radius + otherSpawn.enemyData.status.radius);
            }
        }
    }
    for (const auto& expected : boardStageExpectations)
    {
        const auto found = std::find_if(
            stages.begin(), stages.end(),
            [&expected](const StageData& stage) { return stage.id == expected.id; });
        assert(found != stages.end());
        assert(found->stageType == StageType::Normal);
        assert(found->difficulty == expected.difficulty);
        assert(found->preserveLayout);
        assert(found->enemies.size() == expected.enemyCount);
        std::unordered_map<std::string, int> composition;
        for (std::size_t index = 0; index < found->enemies.size(); ++index)
        {
            const auto& spawn = found->enemies[index];
            ++composition[spawn.enemyId];
            const float radius = spawn.enemyData.status.radius;
            assert(std::abs(spawn.position.y - TableConfig::FIELD_HEIGHT) < 0.001f);
            assert(std::abs(spawn.position.x) + radius < fieldHalfWidth);
            assert(std::abs(spawn.position.z) + radius < fieldHalfDepth);
            assert(planarDistance(spawn.position, Vector3::Zero) >
                radius + playerRadius);
            for (const Vector3& pocketPosition : pockets)
            {
                assert(planarDistance(spawn.position, pocketPosition) >
                    radius + TableConfig::POCKET_RADIUS);
            }
            for (std::size_t other = 0; other < index; ++other)
            {
                const auto& otherSpawn = found->enemies[other];
                assert(planarDistance(spawn.position, otherSpawn.position) >
                    radius + otherSpawn.enemyData.status.radius);
            }
        }
        assert(composition == expected.composition);

        for (const auto& source : found->enemies)
        {
            if (source.enemyData.boardAbility.type ==
                EnemyBoardAbilityType::SwarmAmplifier)
            {
                int nearby = 0;
                for (const auto& ally : found->enemies)
                    if (&ally != &source && planarDistance(
                        source.position, ally.position) <=
                        source.enemyData.boardAbility.radius)
                        ++nearby;
                assert(nearby >= 2 && nearby <= 3);
                assert(EnemyBoardAbilityRules::SwarmStage(nearby) == 1);
            }
            else if (source.enemyData.boardAbility.type ==
                EnemyBoardAbilityType::ProtectionResonator)
            {
                int protectedCount = 0;
                for (const auto& ally : found->enemies)
                    if (&ally != &source && planarDistance(
                        source.position, ally.position) <=
                        source.enemyData.boardAbility.radius)
                        ++protectedCount;
                assert(protectedCount >= 2);
                assert(protectedCount < static_cast<int>(found->enemies.size()) - 1);
            }
            else if (source.enemyData.boardAbility.type ==
                EnemyBoardAbilityType::ImpactBrittle)
            {
                const float rightWallClearance = fieldHalfWidth -
                    source.position.x - source.enemyData.status.radius;
                assert(rightWallClearance >= 5.0f && rightWallClearance <= 15.0f);
            }
        }
        boardStages.push_back(*found);
    }

    StageSelector lockedSelector;
    lockedSelector.Seed(20261112u);
    assert(lockedSelector.SelectStage(
        boardStages, StageType::Normal, 1, std::string()) == nullptr);
    assert(lockedSelector.SelectStage(
        boardStages, StageType::Normal, 3, std::string(),
        { "stage_id_that_does_not_exist" }) == nullptr);

    const std::array<std::pair<int, std::unordered_set<std::string>>, 2>
        selectionExpectations{{
            {3, {"normal_swarm_center", "normal_brittle_wall",
                 "normal_resonance_guard", "normal_dual_threat"}},
            {5, {"normal_swarm_center", "normal_brittle_wall",
                 "normal_resonance_guard", "normal_dual_threat",
                 "normal_resonant_swarm", "normal_triple_encirclement"}},
        }};
    for (const auto& [progress, eligibleIds] : selectionExpectations)
    {
        StageSelector firstSelector;
        StageSelector secondSelector;
        firstSelector.Seed(20261107u + static_cast<unsigned int>(progress));
        secondSelector.Seed(20261107u + static_cast<unsigned int>(progress));
        std::string lastStageId;
        for (int selection = 0; selection < 8; ++selection)
        {
            const StageData* first = firstSelector.SelectStage(
                boardStages, StageType::Normal, progress, lastStageId);
            const StageData* second = secondSelector.SelectStage(
                boardStages, StageType::Normal, progress, lastStageId);
            assert(first != nullptr && second != nullptr);
            assert(first->id == second->id);
            assert(eligibleIds.contains(first->id));
            if (!lastStageId.empty()) assert(first->id != lastStageId);
            lastStageId = first->id;
        }
    }
    GameEncounterBalance encounterBalance;
    encounterBalance.Load("assets/data/encounter_balance.json");
    EncounterThreatInput authoredThreat;
    authoredThreat.stageId = "normal_swarm_center";
    authoredThreat.enemyIds.assign(6, "enemy_normal");
    const auto authoredSnapshot = encounterBalance.Evaluate(authoredThreat);
    assert(authoredSnapshot.layoutSource == "stage_data");
    assert(std::abs(authoredSnapshot.layoutMultiplier - 1.0f) < 0.001f);
    authoredThreat.usesDenseAutoLayout = true;
    const auto denseSnapshot = encounterBalance.Evaluate(authoredThreat);
    assert(denseSnapshot.layoutSource == "dense_auto_layout");
    assert(std::abs(denseSnapshot.layoutMultiplier - 1.25f) < 0.001f);
    authoredThreat.usesMcpOverride = true;
    const auto mcpSnapshot = encounterBalance.Evaluate(authoredThreat);
    assert(mcpSnapshot.layoutSource == "mcp_override");
    assert(std::abs(mcpSnapshot.layoutMultiplier - 1.0f) < 0.001f);
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
	std::cout << "PASS: HP independence, ball/enemy definitions, four synergy resources, save round trips, collision order, pierce/refraction, board-control enemies, six authored normal stages, deterministic stage gating, overlap safety, authored-layout telemetry, directional guard, pocket damage and stage loading.\n";
}
