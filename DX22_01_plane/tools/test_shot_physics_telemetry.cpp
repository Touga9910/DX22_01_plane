#include "../ShotPhysicsTelemetry.h"
#include "../BallPhysicsRules.h"
#include "../CollisionPresentationRules.h"
#include "../CombatPresentationRules.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

int main()
{
	BallPhysicsRules::Body first;
	BallPhysicsRules::Body second;
	first.id = 1;
	second.id = 2;
	first.status.radius = second.status.radius = 1.0f;
	first.status.mass = second.status.mass = 1.0f;
	first.status.restitution = second.status.restitution = 0.8f;
	first.position = { 0.0f, 0.0f, 0.0f };
	second.position = { 1.9f, 0.0f, 0.0f };
	first.velocity = { 1.0f, 0.0f, 0.0f };
	second.velocity = { -1.0f, 0.0f, 0.0f };
	assert(BallPhysicsRules::Pair(first, second));

	// A continuing contact is resolved physically but is not a new event.
	first.position = { 0.0f, 0.0f, 0.0f };
	second.position = { 1.9f, 0.0f, 0.0f };
	first.velocity = { 1.0f, 0.0f, 0.0f };
	second.velocity = { -1.0f, 0.0f, 0.0f };
	assert(!BallPhysicsRules::Pair(first, second));

	// Separation clears the contact episode, so a later impact is new.
	second.position = { 2.1f, 0.0f, 0.0f };
	first.velocity = second.velocity = DirectX::SimpleMath::Vector3::Zero;
	assert(!BallPhysicsRules::Pair(first, second));
	second.position = { 1.9f, 0.0f, 0.0f };
	first.velocity = { 1.0f, 0.0f, 0.0f };
	second.velocity = { -1.0f, 0.0f, 0.0f };
	assert(BallPhysicsRules::Pair(first, second));

	ShotPhysicsTelemetry telemetry;
	telemetry.BeginShot({
		{ 101, 0, "enemy_normal", 10, 10, -2.0f, 4.0f },
		{ 202, 2, "enemy_strong", 20, 20, 2.0f, 4.0f },
		{ 303, 5, "enemy_tank", 30, 30, 6.0f, 4.0f },
	});
	telemetry.RecordEnemyMovement(101, "enemy_normal", 3.0f);
	telemetry.RecordEnemyMovement(101, "enemy_normal", 2.0f);
	telemetry.RecordEnemyMovement(202, "enemy_strong", 7.0f);
	telemetry.RecordEnemyMovement(404, "enemy_spawned", 1.0f);
	telemetry.RecordEnemyEnemyCollision(
		101, "enemy_normal", 202, "enemy_strong", 4.0f, 0.0f, 4.0f);
	telemetry.RecordEnemyEnemyCollision(
		202, "enemy_strong", 101, "enemy_normal", 2.0f, 1.0f, 4.0f);
	telemetry.RecordEnemyEnemyCollision(
		202, "enemy_strong", 303, "enemy_tank", 1.0f, 4.0f, 4.0f);
	telemetry.RecordPhysicsStep(125);
	telemetry.RecordPhysicsStep(75);

	const auto snapshot = telemetry.EndShot();
	assert(snapshot.collisions.size() == 3);
	assert(snapshot.involvedEnemyCount == 3);
	assert(snapshot.pairSummaries.size() == 2);
	assert(snapshot.maximumCollisionsForOnePair == 2);
	assert(std::abs(snapshot.totalEnemyDistance - 13.0f) < 0.001f);
	assert(std::abs(snapshot.maximumEnemyDistance - 7.0f) < 0.001f);
	assert(snapshot.enemyDistances.size() == 4);
	assert(std::abs(snapshot.enemyDistances[2].distance) < 0.001f);
	assert(snapshot.enemyDistances[3].enemy.stableIndex == 6);
	assert(snapshot.physicsProcessingMicroseconds == 200);
	assert(snapshot.physicsStepCount == 2);
	assert(!telemetry.IsActive());

	using Strength = CollisionPresentationRules::Strength;
	assert(CollisionPresentationRules::Classify(-1.0f) == Strength::Weak);
	assert(CollisionPresentationRules::Classify(
		std::numeric_limits<float>::quiet_NaN()) == Strength::Weak);
	assert(CollisionPresentationRules::Classify(1.999f) == Strength::Weak);
	assert(CollisionPresentationRules::Classify(2.0f) == Strength::Medium);
	assert(CollisionPresentationRules::Classify(5.999f) == Strength::Medium);
	assert(CollisionPresentationRules::Classify(6.0f) == Strength::Strong);
	assert(CollisionPresentationRules::Classify(
		std::numeric_limits<float>::infinity()) == Strength::Weak);

	int presentationChainCount = 0;
	for (const auto& collision : snapshot.collisions)
	{
		(void)collision;
		++presentationChainCount;
	}
	assert(presentationChainCount ==
		static_cast<int>(snapshot.collisions.size()));
	assert(CollisionPresentationRules::ChainTier(2) == 0);
	assert(CollisionPresentationRules::ChainTier(3) == 1);
	assert(CollisionPresentationRules::ChainTier(6) == 2);
	assert(CollisionPresentationRules::ChainTier(10) == 3);
	assert(CollisionPresentationRules::ChainTier(15) == 4);
	for (int chain = 1; chain <= 15; ++chain)
	{
		const bool expected =
			chain == 3 || chain == 6 || chain == 10 || chain == 15;
		assert(CollisionPresentationRules::IsChainMilestone(chain) == expected);
	}
	assert(CollisionPresentationRules::MaximumDecorations(0) == 0);
	assert(CollisionPresentationRules::MaximumDecorations(1) == 4);
	assert(CollisionPresentationRules::MaximumDecorations(2) == 8);
	assert(CollisionPresentationRules::MaximumDecorations(3) == 12);
	assert(CollisionPresentationRules::MaximumSparks(0) == 0);
	assert(CollisionPresentationRules::MaximumSparks(1) == 32);
	assert(CollisionPresentationRules::MaximumSparks(2) == 64);
	assert(CollisionPresentationRules::MaximumSparks(3) == 96);
	assert(CollisionPresentationRules::MaximumSparkGenerationPerFrame(0) == 0);
	assert(CollisionPresentationRules::MaximumSparkGenerationPerFrame(1) == 16);
	assert(CollisionPresentationRules::MaximumSparkGenerationPerFrame(2) == 32);
	assert(CollisionPresentationRules::MaximumSparkGenerationPerFrame(3) == 48);

	const auto noFlash = CombatPresentationRules::FlashForDamage(0);
	const auto weakFlash = CombatPresentationRules::FlashForDamage(1);
	const auto mediumFlash = CombatPresentationRules::FlashForDamage(3);
	const auto strongFlash = CombatPresentationRules::FlashForDamage(4);
	assert(noFlash.duration == 0.0f && noFlash.intensity == 0.0f);
	assert(std::abs(weakFlash.duration - 0.08f) < 0.0001f);
	assert(weakFlash.intensity < mediumFlash.intensity);
	assert(std::abs(mediumFlash.duration - 0.12f) < 0.0001f);
	assert(mediumFlash.intensity < strongFlash.intensity);
	assert(std::abs(strongFlash.duration - 0.16f) < 0.0001f);
	assert(CombatPresentationRules::MultiDefeatTier(1) == 0);
	assert(CombatPresentationRules::MultiDefeatTier(2) == 1);
	assert(CombatPresentationRules::MultiDefeatTier(3) == 2);
	assert(CombatPresentationRules::MultiDefeatTier(4) == 3);
	assert(CombatPresentationRules::CanMergeDamage(
		101,
		101,
		0.050f,
		1.0f,
		false));
	assert(!CombatPresentationRules::CanMergeDamage(
		101,
		202,
		0.050f,
		1.0f,
		false));
	assert(!CombatPresentationRules::CanMergeDamage(
		101,
		101,
		0.170f,
		1.0f,
		false));
	assert(CombatPresentationRules::CanMergeDamage(
		101,
		101,
		0.120f,
		1.0f,
		false));
	assert(CombatPresentationRules::MaximumFloatingDamageNumbers == 8);
	assert(std::abs(CombatPresentationRules::DamageFeedbackLifetimeSeconds - 0.70f) < 0.0001f);
	assert(std::abs(CombatPresentationRules::DamageTargetFollowSeconds - 0.25f) < 0.0001f);
	assert(CombatPresentationRules::NormalDamageBackgroundAlpha == 20);
	assert(CombatPresentationRules::DefeatBackgroundAlpha == 38);
	assert(CombatPresentationRules::AbilityBackgroundAlpha == 26);
	assert(CombatPresentationRules::ChainBackgroundAlpha == 64);
	assert(CombatPresentationRules::MultiBreakBackgroundAlpha == 64);
	assert(CombatPresentationRules::ShotResultBackgroundAlpha == 77);
	assert(!CombatPresentationRules::CanMergeDamage(
		101,
		101,
		0.050f,
		1.0f,
		true));

	std::cout << "shot physics telemetry tests passed\n";
	return 0;
}
