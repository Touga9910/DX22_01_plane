#pragma once

#include <algorithm>

struct BallAbilities
{
	bool split = false;
	bool pierce = false;
	bool anchor = false;
};

struct BallStatus
{
	int attack = 1;
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
	return status;
}
