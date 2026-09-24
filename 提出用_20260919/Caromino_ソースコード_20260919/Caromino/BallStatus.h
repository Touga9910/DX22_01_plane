#pragma once

#include <algorithm>

struct BallAbilities
{
	bool split = false;
	bool pierce = false;
	bool anchor = false;
	bool refractAfterPierce = false;
};

struct BallStatus
{
	int attack = 1;
	// Enemy-only compatibility field. Player data loaders and saves force this to zero.
	int defense = 0;
	float mass = 1.0f;
	float radius = 0.0f;
	float restitution = 0.8f;
	float friction = 0.02f;
	// ボール固有の強化軸。HPはラン情報／敵データで管理する。
	float knockbackTransfer = 1.0f;
	int pierceMaxUses = 1;
	float pierceSpeedRetention = 0.75f;
	float anchorBrakeMultiplier = 1.0f;
	float anchorStopSpeedSquared = 0.03f;
	bool anchorKnockbackImmune = false;
	// クッション蓄積を消費した後続プレイヤーボールの速度倍率。
	// 1.0は蓄積能力を持たない通常ボールを表す。
	float cushionChargeSpeedMultiplier = 1.0f;
	// New variants reuse the five existing categories. Zero disables each effect.
	int stopShieldAmount = 0;
	float chainImpactRadius = 0.0f;
	// Category synergy tuning. Zero keeps legacy balls and old saves neutral.
	float heavyFinisherDamagePerCollision = 0.0f;
	int heavyCollisionConsumeAmount = 0;
	int traceDurability = 0;
	float traceUseAngleTolerance = 12.0f;
	float traceUseDistance = 8.0f;
	float traceWidth = 2.0f;
	float tracePierceSpeedMultiplier = 1.1f;
	int tracePierceAttackBonus = 1;
	int tracePierceMaxUsesBonus = 0;
	float tracePierceSpeedRetentionBonus = 0.0f;
	float traceNonPierceSpeedMultiplier = 1.0f;
	int pierceFinisherBaseBonus = 0;
	int pierceFinisherMultiTargetBonus = 0;
	int cushionStackGenerateAmount = 0;
	int cushionMaxStack = 3;
	int cushionStackConsumeAmount = 1;
	int cushionBounceAttackBonus = 0;
	float cushionNonBounceSpeedMultiplier = 1.0f;
	int ricochetFinisherBonusPerUse = 0;
	int anchorPlayerStackGenerate = 0;
	int anchorEnemyStackGenerate = 0;
	float anchorStackRadius = 0.0f;
	int anchorStackMax = 99;
	int anchorFinisherStackConsume = 0;
	int anchorFinisherDamagePerStack = 0;
	int anchorFinisherAoeThreshold = 0;
	float anchorFinisherAoeRadius = 0.0f;
	BallAbilities abilities;
};

inline BallStatus NormalizeBallStatus(BallStatus status)
{
	status.mass = (std::max)(0.0001f, status.mass);
	status.radius = (std::max)(0.0f, status.radius);
	status.restitution = std::clamp(status.restitution, 0.0f, 1.0f);
	status.friction = (std::max)(0.0f, status.friction);
	status.knockbackTransfer = std::clamp(status.knockbackTransfer, 0.0f, 3.0f);
	status.pierceMaxUses = std::clamp(status.pierceMaxUses, 0, 16);
	status.pierceSpeedRetention = std::clamp(status.pierceSpeedRetention, 0.0f, 1.0f);
	status.anchorBrakeMultiplier = std::clamp(status.anchorBrakeMultiplier, 1.0f, 5.0f);
	status.anchorStopSpeedSquared = std::clamp(status.anchorStopSpeedSquared, 0.03f, 1.0f);
	status.cushionChargeSpeedMultiplier =
		std::clamp(status.cushionChargeSpeedMultiplier, 1.0f, 3.0f);
	status.stopShieldAmount = std::clamp(status.stopShieldAmount, 0, 100);
	status.chainImpactRadius = std::clamp(status.chainImpactRadius, 0.0f, 100.0f);
	status.heavyFinisherDamagePerCollision = std::clamp(
		status.heavyFinisherDamagePerCollision, 0.0f, 100.0f);
	status.heavyCollisionConsumeAmount = std::clamp(status.heavyCollisionConsumeAmount, 0, 9999);
	status.traceDurability = std::clamp(status.traceDurability, 0, 99);
	status.traceUseAngleTolerance = std::clamp(status.traceUseAngleTolerance, 1.0f, 89.0f);
	status.traceUseDistance = std::clamp(status.traceUseDistance, 0.1f, 200.0f);
	status.traceWidth = std::clamp(status.traceWidth, 0.1f, 50.0f);
	status.tracePierceSpeedMultiplier = std::clamp(
		status.tracePierceSpeedMultiplier, 1.0f, 3.0f);
	status.tracePierceAttackBonus = std::clamp(status.tracePierceAttackBonus, 0, 1000);
	status.tracePierceMaxUsesBonus = std::clamp(status.tracePierceMaxUsesBonus, 0, 16);
	status.tracePierceSpeedRetentionBonus = std::clamp(
		status.tracePierceSpeedRetentionBonus, 0.0f, 1.0f);
	status.traceNonPierceSpeedMultiplier = std::clamp(
		status.traceNonPierceSpeedMultiplier, 1.0f, 1.25f);
	status.pierceFinisherBaseBonus = std::clamp(status.pierceFinisherBaseBonus, 0, 1000);
	status.pierceFinisherMultiTargetBonus = std::clamp(
		status.pierceFinisherMultiTargetBonus, 0, 1000);
	status.cushionStackGenerateAmount = std::clamp(status.cushionStackGenerateAmount, 0, 99);
	status.cushionMaxStack = std::clamp(status.cushionMaxStack, 1, 99);
	status.cushionStackConsumeAmount = std::clamp(status.cushionStackConsumeAmount, 1, 99);
	status.cushionBounceAttackBonus = std::clamp(status.cushionBounceAttackBonus, 0, 1000);
	status.cushionNonBounceSpeedMultiplier = std::clamp(
		status.cushionNonBounceSpeedMultiplier, 1.0f, 1.25f);
	status.ricochetFinisherBonusPerUse = std::clamp(
		status.ricochetFinisherBonusPerUse, 0, 1000);
	status.anchorPlayerStackGenerate = std::clamp(status.anchorPlayerStackGenerate, 0, 99);
	status.anchorEnemyStackGenerate = std::clamp(status.anchorEnemyStackGenerate, 0, 99);
	status.anchorStackRadius = std::clamp(status.anchorStackRadius, 0.0f, 100.0f);
	status.anchorStackMax = std::clamp(status.anchorStackMax, 1, 9999);
	status.anchorFinisherStackConsume = std::clamp(status.anchorFinisherStackConsume, 0, 9999);
	status.anchorFinisherDamagePerStack = std::clamp(status.anchorFinisherDamagePerStack, 0, 1000);
	status.anchorFinisherAoeThreshold = std::clamp(status.anchorFinisherAoeThreshold, 0, 9999);
	status.anchorFinisherAoeRadius = std::clamp(status.anchorFinisherAoeRadius, 0.0f, 100.0f);
	return status;
}
