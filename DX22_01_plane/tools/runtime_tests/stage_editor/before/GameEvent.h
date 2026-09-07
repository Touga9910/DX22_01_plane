#pragma once

#include <SimpleMath.h>

#include <string>
#include <variant>

struct ShotFiredEvent
{
	std::string ballId;
};

struct EnemyDamageEvent
{
	DirectX::SimpleMath::Vector3 worldPosition{};
	int damage = 0;
	bool defeated = false;
	bool enemyEnemyCollision = false;
};

struct PocketFeedbackEvent
{
	DirectX::SimpleMath::Vector3 worldPosition{};
	bool playerPocket = false;
	bool finisher = false;
	int damage = 0;
};

struct PlayerDamageEvent
{
	std::string source;
	int damage = 0;
	std::string sourceId;
};

struct BallAcquiredEvent
{
	std::string ballId;
};

using GameEvent = std::variant<
	ShotFiredEvent,
	EnemyDamageEvent,
	PocketFeedbackEvent,
	PlayerDamageEvent,
	BallAcquiredEvent>;
