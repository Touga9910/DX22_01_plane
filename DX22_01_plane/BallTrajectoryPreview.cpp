#include "BallTrajectoryPreview.h"

#include "BallCollisionComponent.h"
#include "BallComponent.h"
#include "BallPhysicsComponent.h"
#include "BreakBall.h"
#include "EnemyBall.h"
#include "GameObject.h"
#include "GameWorld.h"
#include "PierceMinion.h"
#include "Pocket.h"
#include "TableFrame.h"
#include "TransformComponent.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
	constexpr int MaximumPreviewTicks = 1200;
	constexpr int MaximumContactsPerTick = 32;
	constexpr std::size_t MaximumPathPoints = 1000;

	void AddPathPoint(
		BallTrajectoryPreview::Result& result,
		const Vector3& position,
		bool force = false)
	{
		if (!result.path.empty())
		{
			if (position == result.path.back()) return;
			if (!force &&
				(position - result.path.back()).LengthSquared() < 1.0f)
			{
				return;
			}
		}
		if (result.path.size() >= MaximumPathPoints)
		{
			result.pathTruncated = true;
			return;
		}
		result.path.push_back(position);
	}

	void EraseReleasedContact(
		BallPhysicsRules::Body& player,
		BallPhysicsRules::Body& other)
	{
		const float separation = player.status.radius +
			other.status.radius + BallCcdGeometry::ReleaseSlop;
		if ((player.position - other.position).LengthSquared() <=
			separation * separation)
		{
			return;
		}
		std::erase(player.pierced, other.id);
		std::erase(other.pierced, player.id);
		std::erase(player.touching, other.id);
		std::erase(other.touching, player.id);
	}
}

#ifndef BALL_TRAJECTORY_PREVIEW_PHYSICS_ONLY
BallTrajectoryPreview::Input BallTrajectoryPreview::Capture(
	const std::vector<BallComponent*>& balls,
	const std::vector<TableFrame*>& tableFrames,
	const std::vector<Pocket*>& pockets,
	const BallComponent& player,
	int pierceMaximumUses,
	float pierceSpeedRetention)
{
	Input input;
	input.playerId = reinterpret_cast<std::uintptr_t>(&player);
	input.balls.reserve(balls.size());
	for (BallComponent* component : balls)
	{
		if (component == nullptr || component->GetGameObject() == nullptr)
			continue;
		GameObject* owner = component->GetGameObject();
		BallCollisionComponent* collision =
			owner->GetComponent<BallCollisionComponent>();
		if (collision == nullptr) continue;

		BallSnapshot snapshot;
		snapshot.physics.id =
			reinterpret_cast<std::uintptr_t>(component);
		snapshot.physics.position = component->GetPosition();
		snapshot.physics.velocity = component->GetVelocity();
		snapshot.physics.status = component->GetStatus();
		if (const BallPhysicsComponent* physical =
			owner->GetComponent<BallPhysicsComponent>())
		{
			snapshot.physics.status.radius = physical->GetRadius();
			snapshot.physics.status.mass = physical->GetMass();
			snapshot.physics.status.restitution =
				physical->GetRestitution();
			snapshot.physics.status.friction = physical->GetFriction();
			snapshot.acceleration = physical->GetAcceleration();
		}
		snapshot.physics.player = component == &player;
		if (const EnemyBall* enemy = owner->GetComponent<EnemyBall>())
		{
			snapshot.physics.enemy = enemy->CanParticipateInCombat();
			snapshot.physics.boss = enemy->IsArmorBoss();
			snapshot.physics.nextKnockbackMultiplier =
				enemy->GetNextKnockbackMultiplier();
			snapshot.guideEnemy = enemy->CanParticipateInCombat();
			snapshot.guideChainCenter = snapshot.guideEnemy;
		}
		if (owner->GetComponent<BreakBall>() != nullptr)
		{
			snapshot.physics.breakBall = true;
			snapshot.guideChainCenter = true;
		}
		snapshot.physics.minion =
			owner->GetComponent<PierceMinion>() != nullptr;
		if (snapshot.physics.player)
		{
			snapshot.physics.pierceLimit = pierceMaximumUses;
			snapshot.physics.pierceRetention = pierceSpeedRetention;
		}
		snapshot.active = collision->CanSimulate();
		input.balls.push_back(std::move(snapshot));
	}

	for (TableFrame* frame : tableFrames)
	{
		if (frame == nullptr || !frame->IsEnabled() ||
			frame->GetGameObject() == nullptr ||
			!frame->GetGameObject()->IsActive())
		{
			continue;
		}
		const auto walls = frame->GetWalls();
		input.walls.insert(
			input.walls.end(), walls.begin(), walls.end());
	}
	input.pockets.reserve(pockets.size());
	for (Pocket* pocket : pockets)
	{
		if (pocket == nullptr || pocket->GetGameObject() == nullptr ||
			!pocket->GetGameObject()->IsActive())
		{
			continue;
		}
		input.pockets.push_back(pocket->GetSphere());
	}
	return input;
}
#endif

BallTrajectoryPreview::Result BallTrajectoryPreview::Predict(
	const Input& input,
	const Vector3& velocity,
	bool skipFirstPlayerFriction)
{
	const auto started = std::chrono::steady_clock::now();
	Result result;
	result.copiedBalls = input.balls.size();
	if (!BallCcdGeometry::Finite(velocity) ||
		velocity.LengthSquared() <= 0.0001f)
	{
		return result;
	}

	std::vector<BallSnapshot> balls = input.balls;
	auto playerIt = std::find_if(
		balls.begin(), balls.end(),
		[&](const BallSnapshot& ball)
		{
			return ball.physics.id == input.playerId && ball.active;
		});
	if (playerIt == balls.end()) return result;
	BallSnapshot& player = *playerIt;
	player.physics.velocity = velocity;
	AddPathPoint(result, player.physics.position, true);
	int stopCount = 0;

	for (int tick = 0; tick < MaximumPreviewTicks; ++tick)
	{
		if (!(tick == 0 && skipFirstPlayerFriction))
		{
			Vector3 exitDirection = Vector3::Zero;
			const Vector3* exit = nullptr;
			for (BallSnapshot& other : balls)
			{
				if (&other == &player || !other.active) continue;
				EraseReleasedContact(player.physics, other.physics);
				if (BallPhysicsRules::PierceExitDirection(
					player.physics, other.physics, exitDirection))
				{
					exit = &exitDirection;
					break;
				}
			}
			if (BallPhysicsRules::PlayerFriction(
				player.physics.velocity,
				player.acceleration,
				player.physics.status,
				player.physics.status.friction,
				stopCount,
				exit))
			{
				AddPathPoint(result, player.physics.position, true);
				result.complete = true;
				break;
			}
		}

		double remaining = 1.0;
		for (int contact = 0;
			contact < MaximumContactsPerTick && remaining > 1.0e-9;
			++contact)
		{
			enum class HitType { None, Pocket, Wall, Ball };
			HitType hitType = HitType::None;
			std::size_t hitBallIndex = 0;
			double earliest = remaining;
			for (const Collision::Sphere& pocket : input.pockets)
			{
				const double time = BallCcdGeometry::PocketTime(
					player.physics.position,
					player.physics.velocity,
					player.physics.status.radius,
					pocket,
					remaining);
				if (time < earliest)
				{
					earliest = time;
					hitType = HitType::Pocket;
				}
			}
			for (const Collision::Segment& wall : input.walls)
			{
				const double time = BallCcdGeometry::WallTime(
					player.physics.position,
					player.physics.velocity,
					player.physics.status.radius,
					wall,
					remaining);
				if (time < earliest)
				{
					earliest = time;
					hitType = HitType::Wall;
				}
			}
			for (std::size_t index = 0; index < balls.size(); ++index)
			{
				BallSnapshot& other = balls[index];
				if (&other == &player || !other.active) continue;
				EraseReleasedContact(player.physics, other.physics);
				if (BallPhysicsRules::IgnoresPiercedPair(
					player.physics, other.physics))
				{
					continue;
				}
				const double time = BallCcdGeometry::SphereTime(
					player.physics.position - other.physics.position,
					player.physics.velocity,
					static_cast<double>(player.physics.status.radius) +
						other.physics.status.radius,
					remaining);
				if (time < earliest)
				{
					earliest = time;
					hitType = HitType::Ball;
					hitBallIndex = index;
				}
			}

			player.physics.position +=
				player.physics.velocity * static_cast<float>(earliest);
			remaining -= earliest;
			if (hitType == HitType::None)
			{
				break;
			}
			AddPathPoint(result, player.physics.position, true);
			if (hitType == HitType::Pocket)
			{
				result.complete = true;
				remaining = 0.0;
				break;
			}
			if (hitType == HitType::Wall)
			{
				result.reachedBlockingContact = true;
				result.complete = true;
				remaining = 0.0;
				break;
			}

			BallSnapshot& other = balls[hitBallIndex];
			const int pierceUsesBefore = player.physics.pierceUses;
			if (!BallPhysicsRules::Pair(player.physics, other.physics))
			{
				// A non-approaching overlap cannot advance the display route.
				remaining = 0.0;
				break;
			}
			AddPathPoint(result, player.physics.position, true);
			if (player.physics.pierceUses > pierceUsesBefore)
			{
				continue;
			}

			result.reachedBlockingContact = true;
			result.complete = true;
			ContactGuide& guide = result.initialContactGuide;
			guide.hitBall = true;
			guide.hitEnemy = other.guideEnemy;
			guide.chainImpactCenter = other.guideChainCenter;
			guide.playerPosition = player.physics.position;
			guide.ballPosition = other.physics.position;
			guide.ballDirection = other.physics.velocity;
			guide.ballDirection.y = 0.0f;
			if (guide.ballDirection.LengthSquared() > 0.0001f)
				guide.ballDirection.Normalize();
			remaining = 0.0;
			break;
		}
		++result.ticks;
		AddPathPoint(result, player.physics.position);
		if (result.complete) break;
	}
	AddPathPoint(result, player.physics.position, true);
	result.milliseconds = std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - started).count();
	return result;
}

#ifndef BALL_TRAJECTORY_PREVIEW_PHYSICS_ONLY
std::uint64_t BallTrajectoryPreview::WorldKey(
	const GameWorld& world,
	int pierceMaximumUses,
	float pierceSpeedRetention)
{
	std::uint64_t hash = 14695981039346656037ull;
	const auto add = [&](std::uint64_t value)
	{
		hash ^= value;
		hash *= 1099511628211ull;
	};
	const auto scalar = [&](float value)
	{
		add(std::bit_cast<std::uint32_t>(value));
	};
	const auto vector = [&](const Vector3& value)
	{
		scalar(value.x); scalar(value.y); scalar(value.z);
	};
	add(static_cast<std::uint64_t>(pierceMaximumUses));
	scalar(pierceSpeedRetention);

	world.ForEachComponent<BallComponent>([&](BallComponent* component)
	{
		if (component == nullptr || component->GetGameObject() == nullptr)
			return;
		GameObject* owner = component->GetGameObject();
		const BallCollisionComponent* collision =
			owner->GetComponent<BallCollisionComponent>();
		if (collision == nullptr) return;
		add(reinterpret_cast<std::uintptr_t>(component));
		add(owner->IsActive());
		add(collision->CanSimulate());
		vector(component->GetPosition());
		vector(component->GetVelocity());
		const BallStatus& status = component->GetStatus();
		scalar(status.radius); scalar(status.mass);
		scalar(status.restitution); scalar(status.friction);
		scalar(status.knockbackTransfer);
		scalar(status.lightweightKnockbackMultiplier);
		scalar(status.lightweightSpeedLossReduction);
		add(status.abilities.pierce);
		add(status.abilities.anchor);
		add(status.abilities.refractAfterPierce);
		add(status.pierceMaxUses);
		scalar(status.pierceSpeedRetention);
		scalar(status.anchorBrakeMultiplier);
		scalar(status.anchorStopSpeedSquared);
		add(status.anchorKnockbackImmune);
		if (const BallPhysicsComponent* physical =
			owner->GetComponent<BallPhysicsComponent>())
		{
			scalar(physical->GetRadius()); scalar(physical->GetMass());
			scalar(physical->GetRestitution());
			scalar(physical->GetFriction());
			vector(physical->GetAcceleration());
		}
		if (const EnemyBall* enemy = owner->GetComponent<EnemyBall>())
		{
			add(enemy->CanParticipateInCombat());
			add(enemy->IsArmorBoss());
			scalar(enemy->GetNextKnockbackMultiplier());
		}
		add(owner->GetComponent<BreakBall>() != nullptr);
		add(owner->GetComponent<PierceMinion>() != nullptr);
	});

	world.ForEachComponent<TableFrame>([&](TableFrame* frame)
	{
		if (frame == nullptr || frame->GetGameObject() == nullptr) return;
		add(reinterpret_cast<std::uintptr_t>(frame));
		add(frame->IsEnabled());
		add(frame->GetGameObject()->IsActive());
		if (const TransformComponent* transform = frame->GetTransform())
		{
			vector(transform->GetPosition());
			vector(transform->GetScale());
			const Quaternion& rotation = transform->GetRotation();
			scalar(rotation.x); scalar(rotation.y);
			scalar(rotation.z); scalar(rotation.w);
		}
	});
	world.ForEachComponent<Pocket>([&](Pocket* pocket)
	{
		if (pocket == nullptr || pocket->GetGameObject() == nullptr) return;
		add(reinterpret_cast<std::uintptr_t>(pocket));
		add(pocket->GetGameObject()->IsActive());
		const Collision::Sphere sphere = pocket->GetSphere();
		vector(sphere.center);
		scalar(sphere.radius);
	});
	return hash;
}
#endif
