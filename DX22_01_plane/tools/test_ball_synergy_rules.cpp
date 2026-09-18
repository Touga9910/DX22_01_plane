#include "../AnchorStackRules.h"
#include "../CushionChargeRules.h"
#include "../HeavyCollisionRules.h"
#include "../PierceTraceRules.h"

#include <cassert>
#include <cmath>
#include <iostream>

using DirectX::SimpleMath::Vector3;

int main()
{
	HeavyCollisionRules::State heavy{};
	assert(heavy.collisionCount == 0); // Player->Enemy has no heavy recording call.
	const bool standardRecorded =
		HeavyCollisionRules::RecordEnemyEnemyCollision(heavy, false);
	assert(!standardRecorded);
	assert(heavy.collisionCount == 0); // 重量以外のショットでは敵同士でも蓄積しない。
	const bool heavyRecorded =
		HeavyCollisionRules::RecordEnemyEnemyCollision(heavy, true);
	assert(heavyRecorded);
	assert(heavy.collisionCount == 1);
	const auto heavyFinish = HeavyCollisionRules::ConsumeForFinisher(heavy, 2.0f, 0);
	assert(heavyFinish.referenced == 1 && heavyFinish.consumed == 1);
	assert(heavyFinish.bonusDamage == 2 && heavy.collisionCount == 0);

	AnchorStackRules::State anchor{};
	int enemyA = 5;
	int enemyB = 0;
	assert(AnchorStackRules::TransferEnemyToEnemy(enemyA, enemyB, 99) == 5);
	assert(enemyA == 0 && enemyB == 5);
	assert(AnchorStackRules::TransferEnemyToPlayer(enemyB, anchor, 99) == 5);
	assert(enemyB == 0 && anchor.playerStacks == 5);
	assert(AnchorStackRules::ConsumePlayerStacks(anchor, 3) == 3);
	assert(anchor.playerStacks == 2);

	PierceTraceRules::State traces{};
	assert(PierceTraceRules::AddTrace(traces, { 0, 0, 0 }, { 20, 0, 0 }, 2));
	assert(PierceTraceRules::AddTrace(traces, { 0, 0, 5 }, { 20, 0, 5 }, 2));
	const auto oldest = traces.traces.front().id;
	assert(PierceTraceRules::AddTrace(traces, { 0, 0, 10 }, { 20, 0, 10 }, 2));
	assert(traces.traces.size() == 2 && traces.overwrittenCount == 1);
	assert(traces.traces.front().id != oldest);

	PierceTraceRules::UseConfig use;
	use.angleToleranceDegrees = 10.0f;
	use.requiredDistance = 6.0f;
	use.width = 1.5f;
	use.nonPierceSpeedMultiplier = 1.05f;
	PierceTraceRules::ShotUseState crossing{};
	Vector3 crossingVelocity(0, 0, 5);
	const Vector3 crossingDirection = crossingVelocity;
	const auto cross = PierceTraceRules::AccumulateMovement(
		traces, crossing, { 10, 0, 0 }, { 10, 0, 10 }, use, false, crossingVelocity);
	assert(!cross.activated && crossingVelocity == crossingDirection);

	PierceTraceRules::ShotUseState parallel{};
	Vector3 parallelVelocity(5, 0, 0);
	const auto firstPart = PierceTraceRules::AccumulateMovement(
		traces, parallel, { 1, 0, 5 }, { 4, 0, 5 }, use, true, parallelVelocity);
	assert(!firstPart.activated);
	const auto secondPart = PierceTraceRules::AccumulateMovement(
		traces, parallel, { 4, 0, 5 }, { 8, 0, 5 }, use, true, parallelVelocity);
	assert(secondPart.activated && parallelVelocity == Vector3(5, 0, 0));
	assert(secondPart.remainingDurability == 1 && traces.durabilityConsumed == 1);

	CushionChargeRules::State cushions{};
	static_assert(CushionChargeRules::RegionCount == 12);
	bool strongConsumed = false;
	int strongUses = 0;
	Vector3 velocity(4, 0, 3);
	auto generated = CushionChargeRules::ApplyStackContact(
		cushions, 0, 4, 3, 1, true, false, 1.2f, 1.03f, 1, 0,
		strongConsumed, strongUses, velocity);
	assert(generated.generated == 3 && cushions[0].stackCount == 3);
	CushionChargeRules::BeginPlayerShot(cushions);
	auto strong = CushionChargeRules::ApplyStackContact(
		cushions, 0, 0, 3, 1, true, false, 1.0f, 1.03f, 2, 0,
		strongConsumed, strongUses, velocity);
	assert(strong.consumed == 1 && strong.damageBonus == 2);
	auto blockedSecondStrong = CushionChargeRules::ApplyStackContact(
		cushions, 0, 0, 3, 1, true, false, 1.0f, 1.03f, 2, 0,
		strongConsumed, strongUses, velocity);
	assert(blockedSecondStrong.kind == CushionChargeRules::UseKind::None);
	assert(cushions[0].stackCount == 2);
	strongConsumed = false;
	strongUses = 0;
	auto finisherOne = CushionChargeRules::ApplyStackContact(
		cushions, 0, 0, 3, 1, true, true, 1.0f, 1.03f, 0, 2,
		strongConsumed, strongUses, velocity);
	auto finisherTwo = CushionChargeRules::ApplyStackContact(
		cushions, 0, 0, 3, 1, true, true, 1.0f, 1.03f, 0, 2,
		strongConsumed, strongUses, velocity);
	assert(finisherOne.damageBonus == 2 && finisherTwo.damageBonus == 2);
	assert(cushions[0].stackCount == 0);

	std::cout << "ball synergy rules: OK\n";
	return 0;
}
