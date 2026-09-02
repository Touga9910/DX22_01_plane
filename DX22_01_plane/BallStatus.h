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
	int maxHp = 3;
	int attack = 1;
	int defense = 0;
	float mass = 1.0f;
	float radius = 0.0f;
	float restitution = 0.8f;
	float friction = 0.02f;
	BallAbilities abilities;
};

inline BallStatus NormalizeBallStatus(BallStatus status)
{
	status.maxHp = (std::max)(1, status.maxHp);
	status.mass = (std::max)(0.0001f, status.mass);
	status.radius = (std::max)(0.0f, status.radius);
	status.restitution = std::clamp(status.restitution, 0.0f, 1.0f);
	status.friction = (std::max)(0.0f, status.friction);
	return status;
}
