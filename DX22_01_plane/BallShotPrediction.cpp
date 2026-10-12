#include "BallShotPrediction.h"
#include "AnchorStackRules.h"
#include "BallCollisionComponent.h"
#include "BallComponent.h"
#include "BallPhysicsComponent.h"
#include "BallPhysicsWorld.h"
#include "EnemyBall.h"
#include "EnemyGimmickRules.h"
#include "BreakBall.h"
#include "NuisanceBall.h"
#include "PierceMinion.h"
#include "json/json.hpp"
#include "GameObject.h"
#include "PlayerBall.h"
#include "PlayerBallData.h"
#include "Pocket.h"
#include "TableFrame.h"

#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numeric>

using namespace DirectX::SimpleMath;
using namespace BallShotPrediction;

namespace
{
    constexpr int MaxTicks = 1200;
    constexpr int MaxTotalSubsteps = 60000;
    constexpr std::size_t MaxPathPoints = 1000;

    std::vector<Ball> Capture(const Input& input)
    {
        std::vector<Ball> result;
        for (auto* component : input.balls)
        {
            if (!component || !component->GetGameObject()) continue;
            auto* owner = component->GetGameObject();
            auto* collision = owner->GetComponent<BallCollisionComponent>();
            if (!collision) continue;
            // Binding normally happens in the real world's fixed update. Reading a
            // snapshot must not repair/change component state; initialization binds it.
            Ball ball;
            ball.physics.id = reinterpret_cast<std::uintptr_t>(component);
            ball.physics.position = component->GetPosition();
            ball.physics.velocity = component->GetVelocity();
            ball.physics.status = component->GetStatus();
            if (const auto* physical = owner->GetComponent<BallPhysicsComponent>())
            {
                ball.physics.status.radius = physical->GetRadius();
                ball.physics.status.mass = physical->GetMass();
                ball.physics.status.restitution = physical->GetRestitution();
                ball.physics.status.friction = physical->GetFriction();
                ball.acceleration = physical->GetAcceleration();
            }
            ball.hp = component->GetHP();
            ball.maxHp = component->GetMaxHP();
            ball.attack = component->GetAttack();
            ball.defense = component->GetDefense();
            ball.defeated = component->IsDefeated();
            if (const auto* player = owner->GetComponent<PlayerBall>())
            {
                ball.physics.player = true;
				ball.defense = 0;
                ball.pocketed = player->IsPocketed();
            }
            if (const auto* enemy = owner->GetComponent<EnemyBall>())
            {
                ball.physics.enemy = true;
                ball.physics.boss = enemy->IsArmorBoss();
                ball.bossState = enemy->GetBossState();
                ball.enemyId = enemy->GetEnemyId();
                ball.pocketed = enemy->IsPocketed();
                ball.frontalMultiplier = enemy->GetFrontalDamageMultiplier();
                ball.pocketDamageRatio = enemy->GetPocketDamageRatio();
                ball.collisionDamageMultipliers = enemy->GetCollisionDamageMultipliers();
                ball.collisionStage = enemy->GetCollisionStage();
                ball.collisionGraceTicks = enemy->GetCollisionCountGraceTicks();
				ball.anchorStacks = enemy->GetAnchorStacks();
				ball.physics.nextKnockbackMultiplier =
					enemy->GetNextKnockbackMultiplier();
            }
            if (const auto* neutral = owner->GetComponent<BreakBall>())
            {
                ball.physics.breakBall = true;
                ball.pocketed = neutral->IsPocketed();
                ball.breakBallUsed = neutral->IsUsed();
            }
			ball.physics.minion = owner->GetComponent<PierceMinion>() != nullptr;
            ball.nuisanceBall = owner->GetComponent<NuisanceBall>() != nullptr;
            ball.active = collision->CanSimulate();
            result.push_back(std::move(ball));
        }
        return result;
    }

    struct World
    {
        Result& result;
        std::vector<std::size_t> order;
        std::vector<Vector3> starts;
        std::vector<Collision::Segment> walls;
        std::vector<Collision::Sphere> pockets;
        Vector3 interior = Vector3::Zero, lastPathVelocity = Vector3::Zero;
        std::uintptr_t playerId = 0;
        float finisherRatio = 0.3f;
        int playerPocketDamage = 1;
        bool gameOver = false;
        const auto& PocketSpheres() const { return pockets; }
        const auto& Walls() const { return walls; }
        const auto& PhysicsBody(std::size_t i) const { return At(i).physics; }

        Ball& At(std::size_t i) { return result.balls[order[i]]; }
        const Ball& At(std::size_t i) const { return result.balls[order[i]]; }
        std::size_t Count() const { return order.size(); }
        bool IsActive(std::size_t i) const { return At(i).active; }
        float Radius(std::size_t i) const { return At(i).physics.status.radius; }
        Vector3 Velocity(std::size_t i) const { return At(i).physics.velocity; }
        bool ShouldContinue() const { return !gameOver; }

        int Attack(const Ball& ball) const
        {
            return ball.attack + (ball.physics.player ? result.shot.collisionBonus : 0);
        }

        void AddPoint(const Ball& ball, bool force = false)
        {
            if (ball.physics.id != playerId || !ball.active) return;
            const auto& p = ball.physics.position;
            const auto& v = ball.physics.velocity;
            const float lengths = lastPathVelocity.Length() * v.Length();
            const bool turned = lengths > 0.0001f && lastPathVelocity.Dot(v) / lengths < 0.9999f;
            if (!result.path.empty() && !force && !turned && (p - result.path.back()).LengthSquared() < 1.0f) return;
            if (!result.path.empty() && p == result.path.back()) return;
            if (result.path.size() >= MaxPathPoints) { result.pathTruncated = true; return; }
            result.path.push_back(p);
            lastPathVelocity = v;
        }

        void BeginSubstep()
        {
            for (std::size_t i = 0; i < Count(); ++i)
            {
                starts[i] = At(i).physics.position;
                AddPoint(At(i));
            }
        }
		void Move(std::size_t i, float interval)
		{
			Ball& ball = At(i);
			const Vector3 from = ball.physics.position;
			ball.physics.position += Velocity(i) * interval;
			if (!ball.physics.enemy) return;
			Vector3 movement = ball.physics.position - from;
			movement.y = 0.0f;
			HeavyCollisionRules::RecordEnemyMovement(
				result.heavyCollisions,
				ball.physics.id,
				movement.Length(),
				result.shotStatus.abilities.heavyDistanceStacks);
		}

        void RecordPreviewReflection(const Ball& ball, const Ball* other = nullptr)
        {
            if (ball.physics.id != playerId) return;
            // Only the first blocking contact supplies the short object-ball guide.
            // Later contacts (including enemies reached after a wall) stay hidden.
            if (result.reflectionPathIndices.empty() && other)
            {
                auto& guide = result.initialContactGuide;
                guide.hitBall = true;
                guide.hitEnemy = other->physics.enemy;
                guide.chainImpactCenter = other->physics.enemy || other->physics.breakBall;
                guide.playerPosition = ball.physics.position;
                guide.ballPosition = other->physics.position;
                guide.ballDirection = other->physics.velocity;
                if (guide.ballDirection.LengthSquared() > 0.0001f) guide.ballDirection.Normalize();
            }
            AddPoint(ball, true);
            if (!result.pathTruncated && !result.path.empty())
                result.reflectionPathIndices.push_back(result.path.size() - 1);
        }

        bool CheckPocket(Ball& ball, const Vector3& start)
        {
            if (ball.physics.boss) return false;
            for (const auto& pocket : pockets)
            {
                if (!BallPhysicsRules::PocketHit(start, ball.physics.position, ball.physics.status.radius, pocket)) continue;
                AddPoint(ball, true);
                const int hpBefore = ball.hp;
				const bool wasDefeated = ball.defeated;
                if (ball.physics.player)
                {
                    ball.hp = (std::max)(0, ball.hp - playerPocketDamage);
                    ball.defeated = ball.hp == 0;
                    gameOver = ball.defeated;
                    if (!ball.defeated)
                    {
                        ball.pocketed = true;
                        ball.physics.position = Vector3(0, -1000, 0);
                    }
                }
                else if (ball.physics.enemy)
                {
                    if (!ball.defeated && ball.pocketDamageRatio > 0.0f)
                        ball.hp = (std::max)(0, ball.hp - BallMechanics::PocketDamage(ball.maxHp, ball.pocketDamageRatio));
                    if (ball.defeated || static_cast<float>(ball.hp) / (std::max)(1, ball.maxHp) <= finisherRatio + 0.0001f)
                    {
                        ball.hp = 0;
                        ball.defeated = true;
                    }
                    else
                    {
                        ball.pocketed = true;
                        ball.physics.position = Vector3(0, -1000, 0);
                    }
                    result.damage += hpBefore - ball.hp;
					if (!wasDefeated && ball.defeated)
						HandleDefeatAnchors(ball);
                }
                if (ball.physics.breakBall)
                {
                    ball.pocketed = true;
                    ball.breakBallUsed = false;
                    ball.physics.position = Vector3(0, -1000, 0);
                }
                ball.active = false;
                ball.physics.velocity = ball.acceleration = Vector3::Zero;
                return true;
            }
            return false;
        }

        void Environment(Ball& ball, const Vector3& start)
        {
            if (!ball.active || CheckPocket(ball, start)) return;
			const auto resolveWall = [&](const Collision::Segment& wall,
				const Vector3& interiorReference)
			{
				const Vector3 velocityBefore = ball.physics.velocity;
				if (!BallPhysicsRules::Wall(
					ball.physics, wall, interiorReference))
				{
					return;
				}
				if (ball.physics.player)
				{
					result.shot.Wall();
					CushionChargeRules::ReflectionConfig config;
					config.generatesStacks =
						result.shot.ballId == "player_cushion_charge";
					config.generationLimit =
						result.shotStatus.cushionReflectionLimit;
					config.stacksPerReflection =
						result.shotStatus.cushionStackGenerateAmount;
					config.consumesStacks =
						result.shot.ballId == "player_bounce";
					config.consumptionLimit =
						result.shotStatus.cushionConsumptionLimit;
					config.reflectedSpeedMultiplier =
						result.shotStatus.cushionReflectionSpeedMultiplier;
					const auto cushion = CushionChargeRules::ApplyValidReflection(
						result.cushionCharges, config, ball.physics.velocity);
					result.cushionBoostConsumed = cushion.consumed > 0;
					result.cushionStrongUses =
						result.cushionCharges.consumedThisShot;
				}
				if (ball.physics.enemy &&
					CushionChargeRules::IsPinballTarget(
						result.cushionCharges, ball.physics.id))
				{
					CushionChargeRules::RegisterPinballCollision(
						result.cushionCharges, ball.physics.id);
					CushionChargeRules::PreservePinballSpeed(
						result.cushionCharges,
						ball.physics.id,
						velocityBefore,
						ball.physics.velocity);
				}
				RecordPreviewReflection(ball);
			};
            if (ball.physics.boss)
                for (const auto& wall : BallPhysicsRules::BossWalls())
					resolveWall(wall, Vector3::Zero);
            for (const auto& wall : walls)
            {
				resolveWall(wall, interior);
            }
            CheckPocket(ball, start);
        }
        void ResolveEnvironment(std::size_t i) { Environment(At(i), starts[i]); }

		AnchorStackRules::EnemyConsumeResult ConsumeEnemyAnchors(
			Ball& target,
			int amount)
		{
			AnchorStackRules::State state;
			state.playerStacks = result.playerAnchorStacks;
			state.conversionProgress = result.anchorConversionProgress;
			const auto consumed = AnchorStackRules::ConsumeEnemyStacks(
				target.anchorStacks,
				state,
				amount,
				result.shotStatus.anchorStackMax);
			result.playerAnchorStacks = state.playerStacks;
			result.anchorConversionProgress = state.conversionProgress;
			return consumed;
		}

		void HandleDefeatAnchors(Ball& target)
		{
			const int automaticConsume =
				AnchorStackRules::DefeatAutoConsumeAmount(target.anchorStacks);
			if (automaticConsume > 0)
				ConsumeEnemyAnchors(target, automaticConsume);
			target.anchorStacks = 0;
		}

		void Damage(Ball& target, int amount, const Vector3& source, bool directional = true)
		{
			if (!target.physics.enemy || target.defeated) return;
			const bool wasDefeated = target.defeated;
			if (directional)
			{
				amount = BallPhysicsRules::DirectionalDamage(amount, target.physics.position, source, target.frontalMultiplier);
				amount = EnemyGimmickRules::ScaleCollisionDamage(
					amount,
					EnemyGimmickRules::CollisionDamageMultiplier(
						target.collisionDamageMultipliers,
						target.collisionStage));
			}
            const int before = target.hp;
            const int applied = target.physics.boss ? BossCombatRules::DirectDamage(amount, target.defense, target.bossState) :
                (std::max)(1, amount - target.defense);
            target.hp = (std::max)(0, target.hp - applied);
            target.defeated = target.hp == 0;
            result.damage += before - target.hp;
			if (!wasDefeated && target.defeated) HandleDefeatAnchors(target);
            // EnemyBall::TakeDamage intentionally preserves velocity after lethal contact.
        }

		void AdvanceCollisionStage(Ball& target, std::uintptr_t otherId)
		{
			if (!target.physics.enemy || target.defeated ||
				target.collisionDamageMultipliers.empty() || otherId == 0 ||
				target.collisionGraceByBall.contains(otherId)) return;
			++target.collisionStage;
			if (target.collisionGraceTicks > 0)
				target.collisionGraceByBall[otherId] = target.collisionGraceTicks;
		}

		void ChainImpact(const Ball& playerBall, const Vector3& center,
			std::uintptr_t excludedTargetId, int attackDamage, bool suppressed)
		{
			if (result.shot.ballId == "player_chain_impact" &&
				!result.heavyFinisherConsumed)
			{
				result.heavyFinisherConsumed = true;
				const auto finisher = HeavyCollisionRules::ConsumeForFinisher(
					result.heavyCollisions,
					result.shotStatus.heavyFinisherDamagePerCollision,
					result.shotStatus.heavyCollisionConsumeAmount);
				result.heavyCollisionCount = result.heavyCollisions.stackCount;
				attackDamage += finisher.bonusDamage;
			}
			const float radius = playerBall.physics.status.chainImpactRadius;
			if (suppressed || radius <= 0.0f || attackDamage <= 0) return;
			struct Target
			{
				Ball* ball = nullptr;
				float distanceSquared = 0.0f;
				std::size_t stableOrder = 0;
			};
			std::vector<Target> targets;
			for (std::size_t index = 0; index < result.balls.size(); ++index)
			{
				Ball& target = result.balls[index];
				if (!target.physics.enemy || target.physics.id == excludedTargetId ||
					target.defeated || target.pocketed || !target.active) continue;
				Vector3 offset = target.physics.position - center;
				offset.y = 0.0f;
				const float distanceSquared = offset.LengthSquared();
				if (distanceSquared <= radius * radius)
					targets.push_back({ &target, distanceSquared, index });
			}
			std::sort(targets.begin(), targets.end(),
				[](const Target& left, const Target& right)
				{
					if (left.distanceSquared != right.distanceSquared)
						return left.distanceSquared < right.distanceSquared;
					return left.stableOrder < right.stableOrder;
				});
			if (targets.size() > 2) targets.resize(2);
			for (const Target& target : targets)
			{
				Damage(*target.ball, attackDamage, center, false);
				++result.chainImpactHits;
			}
		}

        void ResolvePair(std::size_t i, std::size_t j)
        {
            Ball& a = At(i);
            Ball& b = At(j);
            const Vector3 ap = a.physics.position, bp = b.physics.position;
            const Vector3 av = a.physics.velocity, bv = b.physics.velocity;
			const int aPierceUses = a.physics.pierceUses, bPierceUses = b.physics.pierceUses;
			const bool aWasDefeated = a.defeated, bWasDefeated = b.defeated;
            if (BallPhysicsRules::Pair(a.physics, b.physics) && !gameOver)
            {
                // Count a physical ball response once, never the solver's repeated
                // overlap corrections or a straight-through piercing contact.
                if (a.physics.pierceUses == aPierceUses && b.physics.pierceUses == bPierceUses)
                {
                    RecordPreviewReflection(a, &b);
                    RecordPreviewReflection(b, &a);
                }
                if (a.physics.breakBall || b.physics.breakBall)
                {
                    const Vector3 aPosition = a.physics.position;
                    const Vector3 bPosition = b.physics.position;
                    auto hit = [&](Ball& neutral, Ball& boss) {
                        if (!neutral.physics.breakBall || !boss.physics.boss || boss.defeated) return;
                        boss.bossState.HitBreakBall();
                        const int before = boss.hp;
                        boss.hp = (std::max)(0, before - BossCombatRules::BreakBallDamage);
                        boss.defeated = boss.hp == 0;
						if (boss.defeated) HandleDefeatAnchors(boss);
                        result.damage += before - boss.hp;
                        result.bossFixedDamage += before - boss.hp;
                        ++result.breakBallHits;
                        neutral.active = false;
                        neutral.breakBallUsed = true;
                        neutral.pocketed = false;
                        neutral.physics.position = Vector3(0, -1000, 0);
                        neutral.physics.velocity = neutral.acceleration = Vector3::Zero;
                    };
                    hit(a, b); hit(b, a);
                    if (a.physics.breakBall && b.physics.player)
                        ChainImpact(b, aPosition, 0, Attack(b), false);
                    if (b.physics.breakBall && a.physics.player)
                        ChainImpact(a, bPosition, 0, Attack(a), false);
                    if (BallPhysicsRules::StopAnchor(a.physics, a.acceleration)) result.shot.Anchor();
                    if (BallPhysicsRules::StopAnchor(b.physics, b.acceleration)) result.shot.Anchor();
                }
                else
                {
				const bool playerEnemy = (a.physics.player && b.physics.enemy) || (b.physics.player && a.physics.enemy);
				const bool enemyEnemy = a.physics.enemy && b.physics.enemy;
				Ball* targetEnemy = a.physics.enemy ? &a : (b.physics.enemy ? &b : nullptr);
				Ball* playerBall = a.physics.player ? &a : (b.physics.player ? &b : nullptr);
				auto recordPierce = [&](const Ball& playerBall, const Ball& enemyBall)
				{
					result.uniquePiercedEnemies.insert(enemyBall.physics.id);
					PierceTraceRules::RecordPierce(
						result.pierceTraces, enemyBall.physics.id);
					Vector3 direction = playerBall.physics.velocity;
					direction.y = 0.0f;
					if (direction.LengthSquared() <= 0.0001f) return;
					direction.Normalize();
					PierceTraceRules::AddTrace(
						result.pierceTraces,
						playerBall.physics.position,
						playerBall.physics.position +
							direction * result.shotStatus.traceUseDistance);
				};
				if (a.physics.player && a.physics.pierceUses > aPierceUses && b.physics.enemy)
					recordPierce(a, b);
				if (b.physics.player && b.physics.pierceUses > bPierceUses && a.physics.enemy)
					recordPierce(b, a);
				if (playerEnemy && targetEnemy != nullptr &&
					result.shotStatus.abilities.lightweight)
				{
					HeavyCollisionRules::GenerateOncePerEnemy(
						result.heavyCollisions, targetEnemy->physics.id);
				}
				auto damage = BallPhysicsRules::ContactDamage(a.physics, b.physics, Attack(a), Attack(b),
                    a.defeated, b.defeated, result.shot);
				int pinballDamageStage = 0;
				if (enemyEnemy)
				{
					Ball* pinballTarget = nullptr;
					const Vector3* velocityBefore = nullptr;
					if (CushionChargeRules::IsPinballTarget(
						result.cushionCharges, a.physics.id))
					{
						pinballTarget = &a;
						velocityBefore = &av;
					}
					else if (CushionChargeRules::IsPinballTarget(
						result.cushionCharges, b.physics.id))
					{
						pinballTarget = &b;
						velocityBefore = &bv;
					}
					if (pinballTarget != nullptr && velocityBefore != nullptr)
					{
						pinballDamageStage =
							CushionChargeRules::RegisterPinballCollision(
								result.cushionCharges,
								pinballTarget->physics.id);
						CushionChargeRules::PreservePinballSpeed(
							result.cushionCharges,
							pinballTarget->physics.id,
							*velocityBefore,
							pinballTarget->physics.velocity);
					}
					damage.first = HeavyCollisionRules::ScaleEnemyCollisionDamage(
						result.heavyCollisions, damage.first);
					damage.second = HeavyCollisionRules::ScaleEnemyCollisionDamage(
						result.heavyCollisions, damage.second);
					damage.first += pinballDamageStage;
					damage.second += pinballDamageStage;
				}
				if (playerEnemy)
				{
					int bonus = result.synergyDamageBonus;
					if (result.shot.ballId == "player_bounce" && targetEnemy != nullptr &&
						CushionChargeRules::ConsumeEmpoweredHit(
							result.cushionCharges, targetEnemy->physics.id))
					{
						bonus += result.shotStatus.cushionBounceAttackBonus;
					}
					if (result.shot.ballId == "player_pierce_finisher" &&
						!result.uniquePiercedEnemies.empty())
						bonus += result.shotStatus.pierceFinisherBaseBonus +
							(std::max)(0, static_cast<int>(result.uniquePiercedEnemies.size()) - 1) *
							result.shotStatus.pierceFinisherMultiTargetBonus;
					if (result.shot.ballId == "player_ricochet_finisher" &&
						targetEnemy != nullptr)
					{
						const bool activated = CushionChargeRules::ActivatePinball(
							result.cushionCharges,
							targetEnemy->physics.id,
							result.shotStatus.ricochetStackConsume,
							result.shotStatus.pinballCollisionsPerStage,
							result.shotStatus.pinballSpeedRetention);
						if (CushionChargeRules::IsPinballTarget(
							result.cushionCharges, targetEnemy->physics.id))
						{
							CushionChargeRules::RegisterPinballCollision(
								result.cushionCharges,
								targetEnemy->physics.id);
							CushionChargeRules::PreservePinballSpeed(
								result.cushionCharges,
								targetEnemy->physics.id,
								a.physics.enemy ? av : bv,
								targetEnemy->physics.velocity);
						}
						if (activated && playerBall != nullptr)
						{
							playerBall->physics.velocity = Vector3::Zero;
							playerBall->acceleration = Vector3::Zero;
						}
					}
					if (result.shot.ballId == "player_stop_shield" &&
						targetEnemy != nullptr && targetEnemy->anchorStacks > 0)
					{
						const auto consumed = ConsumeEnemyAnchors(
							*targetEnemy,
							result.shotStatus.anchorEnemyConsumeLimit);
						const int shieldGain = AnchorStackRules::CalculateShield(
							consumed.consumed,
							result.shotStatus.stopShieldAmount);
						result.stopShieldGranted = (std::min)(
							result.shotStatus.stopShieldAmount,
							result.stopShieldGranted + shieldGain);
					}
					if (result.shot.ballId == "player_anchor_finisher" &&
						targetEnemy != nullptr && targetEnemy->anchorStacks > 0 &&
						!result.anchorFinisherTriggered)
					{
						result.anchorFinisherTriggered = true;
						const int damagePerStack =
							AnchorStackRules::FinisherDamagePerEnemyStack(
								result.playerAnchorStacks);
						const int available = targetEnemy->anchorStacks;
						const int requested =
							result.shotStatus.anchorFinisherStackConsume <= 0
							? available
							: (std::min)(available,
								result.shotStatus.anchorFinisherStackConsume);
						const int directBonus = requested * damagePerStack;
						bonus += directBonus;
						const auto consumed = ConsumeEnemyAnchors(
							*targetEnemy, requested);
						const int aoeDamage = directBonus / 2;
						if (consumed.consumed >=
								result.shotStatus.anchorFinisherAoeThreshold &&
							aoeDamage > 0 &&
							result.shotStatus.anchorFinisherAoeRadius > 0.0f)
						{
							const float radiusSquared =
								result.shotStatus.anchorFinisherAoeRadius *
								result.shotStatus.anchorFinisherAoeRadius;
							for (Ball& other : result.balls)
							{
								if (!other.physics.enemy ||
									other.physics.id == targetEnemy->physics.id ||
									other.defeated || other.pocketed || !other.active)
								{
									continue;
								}
								Vector3 offset = other.physics.position -
									targetEnemy->physics.position;
								offset.y = 0.0f;
								if (offset.LengthSquared() <= radiusSquared)
									Damage(other, aoeDamage,
										targetEnemy->physics.position, false);
							}
						}
					}
					if (a.physics.enemy) damage.first += bonus;
					else damage.second += bonus;
				}
				Damage(a, damage.first, b.physics.position);
				Damage(b, damage.second, a.physics.position);
				if (playerEnemy || enemyEnemy)
				{
					AdvanceCollisionStage(a, b.physics.id);
					AdvanceCollisionStage(b, a.physics.id);
				}
				if (a.physics.player && b.physics.enemy)
					ChainImpact(a, b.physics.position, b.physics.id, damage.second, bWasDefeated);
				else if (b.physics.player && a.physics.enemy)
					ChainImpact(b, a.physics.position, a.physics.id, damage.first, aWasDefeated);
                if (playerEnemy)
                {
                    if (BallPhysicsRules::StopAnchor(a.physics, a.acceleration)) result.shot.Anchor();
                    if (BallPhysicsRules::StopAnchor(b.physics, b.acceleration)) result.shot.Anchor();
                }
                if (playerEnemy || enemyEnemy) result.shot.Contact(playerEnemy);
                }
                AddPoint(a, true);
                AddPoint(b, true);
            }
            if (a.active && (a.physics.position != ap || a.physics.velocity != av)) Environment(a, ap);
            if (b.active && (b.physics.position != bp || b.physics.velocity != bv)) Environment(b, bp);
        }

        void BeginTick()
        {
            order.clear();
            for (std::size_t i = 0; i < result.balls.size(); ++i)
                if (result.balls[i].active) order.push_back(i);
            for (auto i : order)
            {
                for (auto it = result.balls[i].collisionGraceByBall.begin();
                    it != result.balls[i].collisionGraceByBall.end();)
                {
                    if (--it->second <= 0)
                        it = result.balls[i].collisionGraceByBall.erase(it);
                    else ++it;
                }
                auto& body = result.balls[i].physics;
                body.velocity.y = 0.0f;
                const auto missing = [&](std::uintptr_t id) {
                    return std::none_of(order.begin(), order.end(), [&](std::size_t k) { return result.balls[k].physics.id == id; });
                };
                std::erase_if(body.pierced, missing);
                std::erase_if(body.touching, missing);
            }
            std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                const auto& x = result.balls[a];
                const auto& y = result.balls[b];
                return BallPhysicsRules::OrderingKey(x.physics, x.hp, Attack(x), x.enemyId) <
                    BallPhysicsRules::OrderingKey(y.physics, y.hp, Attack(y), y.enemyId);
            });
            starts.resize(order.size());
        }
    };

    nlohmann::json BallJson(const Ball& ball)
    {
        auto vector = [](const Vector3& v) { return nlohmann::json::array({v.x, v.y, v.z}); };
        return {{"id", ball.physics.id}, {"player", ball.physics.player}, {"enemy_id", ball.enemyId},
            {"position", vector(ball.physics.position)}, {"velocity", vector(ball.physics.velocity)},
            {"hp", ball.hp}, {"active", ball.active}, {"defeated", ball.defeated}, {"pocketed", ball.pocketed},
            {"boss", ball.physics.boss}, {"break_ball", ball.physics.breakBall}, {"break_ball_used", ball.breakBallUsed},
            {"armor", ball.bossState.armor}, {"break_shots_remaining", ball.bossState.shotsRemaining},
            {"break_started_this_shot", ball.bossState.startedThisShot}};
    }

	nlohmann::json CushionJson(const CushionChargeRules::State& charges)
	{
		return {
			{ "stack_count", charges.stackCount },
			{ "pending_setup_bonus", charges.pendingSetupBonus },
			{ "active_setup_bonus", charges.activeSetupBonus },
			{ "generated_this_shot", charges.generatedThisShot },
			{ "consumed_this_shot", charges.consumedThisShot },
			{ "empowered_hits_remaining", charges.empoweredHitsRemaining },
			{ "pinball_target_id", charges.pinballTargetId },
			{ "pinball_collision_count", charges.pinballCollisionCount },
			{ "pinball_collisions_per_stage", charges.pinballCollisionsPerStage },
		};
	}

    bool VerificationEnabled()
    {
#ifdef _DEBUG
        char* flag = nullptr;
        std::size_t size = 0;
        _dupenv_s(&flag, &size, "DX22_TEST_PREDICTION");
        const bool enabled = flag != nullptr && std::string(flag) == "1";
        free(flag);
        return enabled;
#else
        return false;
#endif
    }
}

Result BallShotPrediction::Predict(const Input& input, const PlayerBall& player, const Vector3& velocity, bool skipFirstPlayerFriction, const PlayerBallData* offer)
{
    const auto started = std::chrono::steady_clock::now();
    Result result;
    if (!std::isfinite(velocity.x) || !std::isfinite(velocity.z) || velocity.LengthSquared() <= 0.0001f) return result;
    result.balls = Capture(input);
	result.cushionCharges = input.cushionCharges;
	result.pierceTraces = input.pierceTraces;
	result.heavyCollisions = input.heavyCollisions;
	result.heavyCollisionCount = input.heavyCollisions.stackCount;
	result.playerAnchorStacks = input.playerAnchorStacks;
	result.anchorConversionProgress = input.anchorConversionProgress;
	CushionChargeRules::BeginPlayerShot(result.cushionCharges);
	HeavyCollisionRules::BeginShot(result.heavyCollisions);
	PierceTraceRules::BeginShot(result.pierceTraces);
    result.shot = input.shot;
    if (offer) result.shot.ballId = offer->definitionId;
    World world{ result };
    world.playerId = reinterpret_cast<std::uintptr_t>(player.GetBall());
    world.finisherRatio = input.pocketFinisherRatio;
    world.playerPocketDamage = input.playerPocketDamage;
    bool found = false;
    for (auto& ball : result.balls)
    {
        if (ball.physics.boss) ball.bossState.BeginShot();
        if (ball.physics.id != world.playerId) continue;
        if (!ball.active) return result;
        ball.physics.velocity = velocity;
		result.shotStatus = ball.physics.status;
        ball.physics.pierceLimit = input.pierceMaximumUses;
        ball.physics.pierceRetention = input.pierceSpeedRetention;
        if (offer)
        {
            ball.physics.status = offer->status;
			result.shotStatus = offer->status;
			ball.defense = 0;
            ball.physics.pierceLimit = offer->status.pierceMaxUses;
            ball.physics.pierceRetention = offer->status.pierceSpeedRetention;
            if (offer->definitionId == "player_pierce" && input.hasPierceBallCharger)
            { ++ball.physics.pierceLimit; ball.physics.pierceRetention = 1.0f; }
        }
        ball.attack = ball.physics.status.attack + input.relicAttackBonus +
            player.GetAuraStatusEffects().GetAttackModifier();
		result.stopShieldGranted = 0;
        world.AddPoint(ball, true);
        found = true;
    }
    if (!found) return result;
	if (result.shot.ballId == "player_collision_boost" &&
		result.shotStatus.heavySetupStackConsume > 0 &&
		result.heavyCollisions.stackCount >=
			result.shotStatus.heavySetupStackConsume)
	{
		HeavyCollisionRules::Consume(
			result.heavyCollisions,
			result.shotStatus.heavySetupStackConsume);
		HeavyCollisionRules::ArmNextEnemyCollisionMultiplier(
			result.heavyCollisions,
			result.shotStatus.heavyEnemyCollisionMultiplier);
	}
	if (result.shot.ballId == "player_trace_driver" ||
		result.shot.ballId == "player_minion_finisher")
	{
		const bool finisher = result.shot.ballId == "player_minion_finisher";
		const int consumeLimit = finisher
			? PierceTraceRules::MaxStackCount
			: result.shotStatus.pierceStackConsumeLimit;
		const int consumed = PierceTraceRules::ConsumeStacks(
			result.pierceTraces,
			consumeLimit);
		const auto launches = PierceTraceRules::BuildMinionLaunches(
			result.pierceTraces,
			consumed,
			finisher);
		for (std::size_t index = 0; index < launches.size(); ++index)
		{
			const auto& launch = launches[index];
			Ball minion;
			minion.physics.id = (std::numeric_limits<std::uintptr_t>::max)() - index;
			minion.physics.position = launch.start;
			minion.physics.velocity = launch.direction *
				PierceTraceRules::MinionLaunchSpeed;
			minion.physics.status = result.shotStatus;
			minion.physics.status.abilities = {};
			minion.physics.status.attack = launch.attack;
			minion.physics.status.knockbackTransfer = launch.knockbackMultiplier;
			minion.physics.minion = true;
			minion.hp = minion.maxHp = 1;
			minion.attack = launch.attack;
			minion.playerSimulation = false;
			result.balls.push_back(std::move(minion));
		}
	}
    for (auto* frame : input.tableFrames)
    {
        if (!frame->IsEnabled() || !frame->GetGameObject()->IsActive()) continue;
        const auto walls = frame->GetWalls();
        world.walls.insert(world.walls.end(), walls.begin(), walls.end());
    }
    for (const auto& wall : world.walls) world.interior += wall.start + wall.end;
    if (!world.walls.empty()) world.interior /= static_cast<float>(world.walls.size() * 2);
    for (auto* pocket : input.pockets)
        if (pocket && pocket->GetGameObject() && pocket->GetGameObject()->IsActive()) world.pockets.push_back(pocket->GetSphere());
    int stoppedTicks = 0;
    for (int tick = 0; tick < MaxTicks; ++tick)
    {
        for (auto& ball : result.balls)
        {
            if (!ball.active) continue;
            if (ball.physics.player && !(tick == 0 && skipFirstPlayerFriction))
            {
                Vector3 exitDirection;
                bool exitingPierce = false;
                if (ball.physics.status.abilities.pierce && !ball.physics.pierced.empty())
                    for (const auto& other : result.balls)
                        if (other.active && BallPhysicsRules::PierceExitDirection(ball.physics, other.physics, exitDirection))
                        { exitingPierce = true; break; }
                if (ball.playerSimulation || exitingPierce)
                    ball.playerSimulation = !BallPhysicsRules::PlayerFriction(ball.physics.velocity, ball.acceleration,
                    ball.physics.status, ball.physics.status.friction, ball.stopCount,
                    exitingPierce ? &exitDirection : nullptr);
            }
            if (ball.physics.enemy || ball.physics.minion ||
				ball.physics.breakBall || ball.nuisanceBall)
                BallPhysicsRules::EnemyFriction(ball.physics.velocity, ball.physics.status.friction);
        }
        world.BeginTick();
        const auto step = ContinuousBallStepper::Step(world);
        ++result.ticks;
        result.substeps += step.substeps;
        const bool stopped = std::all_of(result.balls.begin(), result.balls.end(), [](const Ball& b) {
            return b.physics.velocity.LengthSquared() < 0.03f;
        });
        stoppedTicks = stopped ? stoppedTicks + 1 : 0;
        if (step.limitReached) break;
        if (world.gameOver || stoppedTicks >= 11) { result.complete = true; break; }
        if (result.substeps >= MaxTotalSubsteps) break;
    }
    for (const auto& ball : result.balls) world.AddPoint(ball, true);
	HeavyCollisionRules::EndShot(result.heavyCollisions);
	result.heavyCollisionCount = result.heavyCollisions.stackCount;
    result.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return result;
}

std::uint64_t BallShotPrediction::WorldKey(const Input& input)
{
    std::uint64_t hash = 14695981039346656037ull;
    auto add = [&](std::uint64_t value) { hash ^= value; hash *= 1099511628211ull; };
    auto scalar = [&](float value) { add(std::bit_cast<std::uint32_t>(value)); };
    auto vector = [&](const Vector3& v) { scalar(v.x); scalar(v.y); scalar(v.z); };
    for (const auto& ball : Capture(input))
    {
        add(ball.physics.id); add(ball.active); add(ball.hp); add(ball.maxHp); add(ball.attack); add(ball.defense);
        add(ball.defeated); add(ball.pocketed);
        add(ball.physics.boss); add(ball.physics.breakBall); add(ball.breakBallUsed);
		add(ball.anchorStacks);
        add(ball.bossState.armor); add(ball.bossState.shotsRemaining); add(ball.bossState.startedThisShot);
        vector(ball.physics.position); vector(ball.physics.velocity);
        const auto& s = ball.physics.status;
        scalar(s.radius); scalar(s.mass); scalar(s.restitution); scalar(s.friction); scalar(s.knockbackTransfer);
		scalar(s.lightweightKnockbackMultiplier);
		scalar(s.lightweightSpeedLossReduction);
		add(s.heavySetupStackConsume);
		scalar(s.heavyEnemyCollisionMultiplier);
		add(s.heavyCollisionConsumeAmount);
		scalar(s.heavyFinisherDamagePerCollision);
        add(s.abilities.pierce); add(s.abilities.anchor); add(s.pierceMaxUses); scalar(s.pierceSpeedRetention);
        scalar(s.anchorBrakeMultiplier); scalar(s.anchorStopSpeedSquared); add(s.anchorKnockbackImmune);
        scalar(s.cushionChargeSpeedMultiplier);
		add(s.stopShieldAmount); scalar(s.chainImpactRadius); add(s.abilities.refractAfterPierce);
		add(s.traceDurability); scalar(s.traceUseAngleTolerance); scalar(s.traceUseDistance); scalar(s.traceWidth);
		add(s.tracePierceMaxUsesBonus); scalar(s.tracePierceSpeedRetentionBonus); scalar(s.traceNonPierceSpeedMultiplier);
		add(s.pierceStackConsumeLimit);
		add(s.cushionStackGenerateAmount); add(s.cushionMaxStack); add(s.cushionStackConsumeAmount);
		add(s.cushionBounceAttackBonus); scalar(s.cushionNonBounceSpeedMultiplier);
		add(s.cushionReflectionLimit); add(s.cushionConsumptionLimit);
		scalar(s.cushionReflectionSpeedMultiplier); add(s.reflectionSetupAmount);
		add(s.ricochetStackConsume); add(s.pinballCollisionsPerStage);
		scalar(s.pinballSpeedRetention);
		add(s.anchorPlayerStackGenerate); add(s.anchorEnemyStackGenerate); scalar(s.anchorStackRadius);
		add(s.anchorStackMax); add(s.anchorFinisherStackConsume);
		add(s.anchorFinisherDamagePerStack); add(s.anchorFinisherAoeThreshold); scalar(s.anchorFinisherAoeRadius);
		add(s.anchorEnemyConsumeLimit);
        scalar(ball.frontalMultiplier); scalar(ball.pocketDamageRatio);
        add(ball.collisionStage); add(ball.collisionGraceTicks); add(ball.nuisanceBall);
        for (float multiplier : ball.collisionDamageMultipliers) scalar(multiplier);
    }
    for (bool owned : input.shot.relics) add(owned);
    for (unsigned char c : input.shot.ballId) add(c);
	if (input.hasCurrentBall)
		add(static_cast<std::uint64_t>(input.currentBallCategory));
    scalar(input.pocketFinisherRatio); add(input.playerPocketDamage);
	add(input.cushionCharges.stackCount);
	add(input.cushionCharges.pendingSetupBonus);
	add(input.cushionCharges.activeSetupBonus);
	add(input.heavyCollisions.stackCount);
	scalar(input.heavyCollisions.pendingEnemyCollisionMultiplier);
	scalar(input.heavyCollisions.activeEnemyCollisionMultiplier);
	add(input.playerAnchorStacks);
	add(input.anchorConversionProgress);
	add(input.pierceTraces.stackCount);
	for (const auto& trace : input.pierceTraces.traces)
	{
		add(trace.id); vector(trace.start); vector(trace.end); vector(trace.direction);
	}
    for (auto* frame : input.tableFrames)
    {
        add(frame->IsEnabled()); add(frame->GetGameObject()->IsActive());
        for (const auto& wall : frame->GetWalls()) { vector(wall.start); vector(wall.end); }
    }
    for (auto* pocket : input.pockets)
    {
        add(pocket->GetGameObject()->IsActive());
        const auto sphere = pocket->GetSphere(); vector(sphere.center); scalar(sphere.radius);
    }
    return hash;
}

void BallShotPrediction::WriteVerificationPrediction(const Input& input, const PlayerBall& player, const Vector3& velocity, bool skip)
{
    if (!VerificationEnabled()) return;
    const auto before = WorldKey(input);
    const auto result = Predict(input, player, velocity, skip);
    nlohmann::json balls = nlohmann::json::array();
    for (const auto& ball : result.balls) balls.push_back(BallJson(ball));
    const nlohmann::json data = {{"physics_model", BallPhysicsWorld::ModelName}, {"complete", result.complete},
        {"path_truncated", result.pathTruncated}, {"path_points", result.path.size()},
        {"reflection_path_indices", result.reflectionPathIndices},
        {"preview_one_reflection_points", result.PreviewPointCount(1)},
        {"preview_one_reflection_limited", result.PreviewReachesLimit(1)},
        {"preview_direct_points", result.PreviewPointCount(0)},
        {"preview_direct_hit_ball", result.initialContactGuide.hitBall},
        {"preview_direct_hit_enemy", result.initialContactGuide.hitEnemy},
        {"preview_chain_impact_center", result.initialContactGuide.chainImpactCenter},
        {"ticks", result.ticks}, {"substeps", result.substeps}, {"milliseconds", result.milliseconds},
        {"world_unchanged", before == WorldKey(input)}, {"balls", balls},
        {"cushions", CushionJson(result.cushionCharges)},
		{"cushion_boost_consumed", result.cushionBoostConsumed},
		{"chain_impact_hits", result.chainImpactHits},
		{"stop_shield_granted", result.stopShieldGranted},
        {"player_enemy_contacts", result.shot.playerEnemyContacts}, {"enemy_enemy_contacts", result.shot.enemyEnemyContacts}};
    std::filesystem::create_directories("runtime");
    std::ofstream("runtime/shot_prediction_expected.json") << data.dump(2);
}

void BallShotPrediction::WriteVerificationActual(const Input& input)
{
    if (!VerificationEnabled()) return;
    nlohmann::json balls = nlohmann::json::array();
    for (const auto& ball : Capture(input)) balls.push_back(BallJson(ball));
    const nlohmann::json data = {{"balls", balls},
        {"cushions", CushionJson(input.cushionCharges)},
		{"cushion_boost_consumed", input.cushionBoostConsumed},
        {"player_enemy_contacts", input.playerEnemyContacts},
        {"enemy_enemy_contacts", input.enemyEnemyContacts}};
    std::filesystem::create_directories("runtime");
    std::ofstream("runtime/shot_prediction_actual.json") << data.dump(2);
}
