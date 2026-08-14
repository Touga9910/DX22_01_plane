#pragma once

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
