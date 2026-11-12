#pragma once

#include "BallPhysicsRules.h"
#include "Collision.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class BallComponent;
class GameWorld;
class Pocket;
class TableFrame;

namespace BallTrajectoryPreview
{
	struct BallSnapshot final
	{
		BallPhysicsRules::Body physics;
		DirectX::SimpleMath::Vector3 acceleration =
			DirectX::SimpleMath::Vector3::Zero;
		bool active = true;
		bool guideEnemy = false;
		bool guideChainCenter = false;
	};

	struct Input final
	{
		std::vector<BallSnapshot> balls;
		std::vector<Collision::Segment> walls;
		std::vector<Collision::Sphere> pockets;
		std::uintptr_t playerId = 0;
	};

	struct ContactGuide final
	{
		bool hitBall = false;
		bool hitEnemy = false;
		bool chainImpactCenter = false;
		DirectX::SimpleMath::Vector3 playerPosition =
			DirectX::SimpleMath::Vector3::Zero;
		DirectX::SimpleMath::Vector3 ballPosition =
			DirectX::SimpleMath::Vector3::Zero;
		DirectX::SimpleMath::Vector3 ballDirection =
			DirectX::SimpleMath::Vector3::Zero;
	};

	struct Result final
	{
		std::vector<DirectX::SimpleMath::Vector3> path;
		ContactGuide initialContactGuide;
		bool reachedBlockingContact = false;
		bool complete = false;
		bool pathTruncated = false;
		int ticks = 0;
		std::size_t copiedBalls = 0;
		double milliseconds = 0.0;
	};

	// Captures only physical values needed before the first blocking contact.
	// It deliberately excludes HP, damage, buffs, rewards and shot evaluation.
	Input Capture(
		const std::vector<BallComponent*>& balls,
		const std::vector<TableFrame*>& tableFrames,
		const std::vector<Pocket*>& pockets,
		const BallComponent& player,
		int pierceMaximumUses,
		float pierceSpeedRetention);

	// Simulates the displayed incoming route only. Shared collision/friction
	// rules are used, but combat state and post-contact chains are not created.
	Result Predict(
		const Input& input,
		const DirectX::SimpleMath::Vector3& velocity,
		bool skipFirstPlayerFriction);

	// Allocation-free signature used by the per-frame preview cache check.
	std::uint64_t WorldKey(
		const GameWorld& world,
		int pierceMaximumUses,
		float pierceSpeedRetention);
}
