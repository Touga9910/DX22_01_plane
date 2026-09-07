#pragma once

#include "BallMechanics.h"
#include "Collision.h"
#include "BallCcdGeometry.h"
#include "TableConfig.h"
#include <algorithm>
#include <cstdint>
#include <tuple>
#include <string>
#include <vector>

// Value-only rules: usable by real components and a disposable prediction world.
namespace BallPhysicsRules
{
    using DirectX::SimpleMath::Vector3;
    struct Body
    {
        std::uintptr_t id = 0;
        Vector3 position = Vector3::Zero, velocity = Vector3::Zero;
        BallStatus status;
        bool player = false, enemy = false, breakBall = false, boss = false;
        int pierceUses = 0, pierceLimit = 0;
        float pierceRetention = 0.75f;
        std::vector<std::uintptr_t> pierced;
        std::vector<std::uintptr_t> touching;
    };

    // Closed rectangle only for the boss; ordinary balls retain pocket openings.
    inline auto BossWalls()
    {
        const float x = TableConfig::GetFieldWidth() * 0.5f;
        const float z = TableConfig::GetFieldDepth() * 0.5f;
        const float y = TableConfig::FIELD_HEIGHT;
        return std::array<Collision::Segment, 4>{{
            {{-x,y,-z},{x,y,-z}}, {{x,y,-z},{x,y,z}},
            {{x,y,z},{-x,y,z}}, {{-x,y,z},{-x,y,-z}}
        }};
    }

    // Only contacts already traversed by piercing bypass ordinary separation.
    // Include exhausted pierce charges and defeated enemies still on the table.
    inline bool PierceExitDirection(const Body& player, const Body& enemy, Vector3& direction)
    {
        if (!player.player || !player.status.abilities.pierce || !enemy.enemy ||
            std::find(player.pierced.begin(), player.pierced.end(), enemy.id) == player.pierced.end()) return false;
        const float separation = player.status.radius + enemy.status.radius + BallCcdGeometry::ReleaseSlop;
        Vector3 away = player.position - enemy.position;
        away.y = 0;
        if (away.LengthSquared() > separation * separation) return false;
        direction = player.velocity;
        direction.y = 0;
        if (direction.LengthSquared() < 0.000001f) direction = away;
        if (direction.LengthSquared() < 0.000001f) direction = Vector3::UnitZ;
        direction.Normalize();
        return true;
    }

    inline bool PlayerFriction(Vector3& velocity, Vector3& acceleration,
        const BallStatus& status, float friction, int& stopCount, const Vector3* pierceExit = nullptr)
    {
        if (pierceExit)
        {
            // Stay above the world's sqrt(0.03) stopped threshold while inside
            // a pierced enemy. Resume ordinary braking immediately after exit.
            constexpr float minimumExitSpeed = 0.2f;
            const Vector3 previous = velocity;
            velocity = *pierceExit * (std::max)(minimumExitSpeed, velocity.Length() - friction);
            acceleration = velocity - previous;
            stopCount = 0;
            return false;
        }
        const float threshold = status.abilities.anchor ? status.anchorStopSpeedSquared : 0.03f;
        if (velocity.LengthSquared() < threshold) ++stopCount;
        else
        {
            stopCount = 0;
            Vector3 deceleration = -velocity;
            deceleration.Normalize();
            const float brake = status.abilities.anchor ? status.anchorBrakeMultiplier : 1.0f;
            acceleration = deceleration * (std::min)(friction * brake, velocity.Length());
            velocity += acceleration;
        }
        if (stopCount <= 10) return false;
        velocity = acceleration = Vector3::Zero;
        return true;
    }

    inline void EnemyFriction(Vector3& velocity, float friction)
    {
        if (velocity.LengthSquared() <= 0.001f) return;
        if (velocity.LengthSquared() < 0.03f) velocity = Vector3::Zero;
        else
        {
            Vector3 deceleration = -velocity;
            deceleration.Normalize();
            velocity += deceleration * friction;
        }
    }

    inline bool PocketHit(Vector3 start, Vector3 end, float radius, const Collision::Sphere& pocket)
    {
        start.y = end.y = pocket.center.y;
        const float trigger = radius + pocket.radius + BallCcdGeometry::ContactSlop;
        return Collision::DistanceSquaredPointToSegment(pocket.center, { start, end }) <= trigger * trigger;
    }

    inline bool Wall(Body& body, const Collision::Segment& wall, const Vector3& interior)
    {
        float projection;
        const Vector3 contact = BallCcdGeometry::ClosestXZ(body.position, wall, &projection);
        Vector3 normal = body.position - contact;
        const float distance = normal.Length();
        if (distance > body.status.radius + BallCcdGeometry::ContactSlop) return false;
        const Vector3 inward = BallCcdGeometry::InwardNormal(wall, interior);
        // The flat face points into the table. End caps use their radial normal,
        // preventing a glancing hit from snapping sideways onto the infinite line.
        if (projection > 0.0f && projection < 1.0f) normal = inward;
        else if (distance > 0.000001f) normal /= distance;
        else normal = inward;
        if (distance < body.status.radius || normal.Dot(body.position - contact) < 0)
            body.position = contact + normal * body.status.radius;
        const float dot = Collision::Dot(body.velocity, normal);
        if (dot >= -BallCcdGeometry::ApproachEpsilon) return false;
        body.velocity = (body.velocity - normal * (2.0f * dot)) * body.status.restitution;
        return true;
    }

    inline bool AnchorLocked(const Body& body)
    {
        return body.player && body.status.abilities.anchor && body.status.anchorKnockbackImmune &&
            body.velocity.LengthSquared() <= 0.0001f;
    }

    inline bool IgnoresPiercedPair(const Body& a, const Body& b)
    {
        const float separation = a.status.radius + b.status.radius + BallCcdGeometry::ReleaseSlop;
        if ((a.position - b.position).LengthSquared() > separation * separation) return false;
        return std::find(a.pierced.begin(), a.pierced.end(), b.id) != a.pierced.end() ||
            std::find(b.pierced.begin(), b.pierced.end(), a.id) != b.pierced.end();
    }

    // True means a NEW approaching contact episode (one attack event). Further
    // solver iterations can still adjust velocity/position without dealing damage.
    inline bool Pair(Body& a, Body& b)
    {
        Vector3 normal = a.position - b.position;
        const float distance = normal.Length();
        const float separation = a.status.radius + b.status.radius;
        if (distance > separation + BallCcdGeometry::ReleaseSlop)
        {
            std::erase(a.pierced, b.id);
            std::erase(b.pierced, a.id);
            std::erase(a.touching, b.id);
            std::erase(b.touching, a.id);
        }
        if (distance > separation + BallCcdGeometry::ContactSlop || IgnoresPiercedPair(a, b)) return false;
        if (distance > 0.0001f) normal /= distance;
        else
        {
            normal = b.velocity - a.velocity;
            if (normal.LengthSquared() > 0.0001f) normal.Normalize();
            else normal = Vector3::UnitX;
        }
        const bool aPierces = a.player && b.enemy && a.status.abilities.pierce && a.pierceUses < a.pierceLimit;
        const bool bPierces = b.player && a.enemy && b.status.abilities.pierce && b.pierceUses < b.pierceLimit;
        const bool aLocked = AnchorLocked(a), bLocked = AnchorLocked(b);
        if (!aPierces && !bPierces)
        {
            const float overlap = (std::max)(0.0f, separation - distance);
            if (!aLocked) a.position += normal * overlap * (bLocked ? 1.0f : 0.5f);
            if (!bLocked) b.position += -normal * overlap * (aLocked ? 1.0f : 0.5f);
        }
        const float av = Collision::Dot(a.velocity, normal), bv = Collision::Dot(b.velocity, normal);
        if (av - bv >= -BallCcdGeometry::ApproachEpsilon) return false;
        const bool newContact = std::find(a.touching.begin(), a.touching.end(), b.id) == a.touching.end() &&
            std::find(b.touching.begin(), b.touching.end(), a.id) == b.touching.end();
        if (newContact) { a.touching.push_back(b.id); b.touching.push_back(a.id); }
        if (aPierces || bPierces)
        {
            Body& piercing = aPierces ? a : b;
            ++piercing.pierceUses;
            piercing.pierced.push_back(aPierces ? b.id : a.id);
            piercing.velocity *= piercing.pierceRetention;
        }
        else
        {
            const float restitution = a.player ? a.status.restitution :
                (b.player ? b.status.restitution : (std::min)(a.status.restitution, b.status.restitution));
            const auto response = BallMechanics::ResolveNormalImpact(av, bv, a.status.mass, b.status.mass,
                restitution, a.player && (b.enemy || b.breakBall) ? a.status.knockbackTransfer : 1.0f,
                b.player && (a.enemy || a.breakBall) ? b.status.knockbackTransfer : 1.0f, aLocked, bLocked);
            a.velocity += normal * (response.first - av);
            b.velocity += normal * (response.second - bv);
            if (!newContact)
            {
                // The legacy 2*restitution response may retain closing velocity
                // for low restitution. Project persistent contacts to non-closing
                // motion without introducing another attack or artificial bounce.
                const float closing = (a.velocity - b.velocity).Dot(normal);
                if (closing < 0.0f)
                {
                    const float aWeight = aLocked ? 0.0f : 1.0f / a.status.mass;
                    const float bWeight = bLocked ? 0.0f : 1.0f / b.status.mass;
                    const float total = aWeight + bWeight;
                    if (total > 0.0f)
                    {
                        a.velocity -= normal * (closing * aWeight / total);
                        b.velocity += normal * (closing * bWeight / total);
                    }
                }
                if (a.player && (b.enemy || b.breakBall) && a.status.abilities.anchor) a.velocity = Vector3::Zero;
                if (b.player && (a.enemy || a.breakBall) && b.status.abilities.anchor) b.velocity = Vector3::Zero;
            }
        }
        return newContact;
    }

    inline bool StopAnchor(Body& body, Vector3& acceleration)
    {
        if (!body.player || !body.status.abilities.anchor) return false;
        body.velocity = acceleration = Vector3::Zero;
        return true;
    }

    inline int DirectionalDamage(int damage, const Vector3& target, const Vector3& source, float multiplier)
    {
        Vector3 incoming = source - target;
        incoming.y = 0.0f;
        if (incoming.LengthSquared() <= 0.0001f) return damage;
        incoming.Normalize();
        return BallMechanics::DirectionalDamage(damage, incoming.Dot(-Vector3::UnitZ), multiplier);
    }

    template<class Shot>
    std::pair<int, int> ContactDamage(const Body& a, const Body& b, int attackA, int attackB,
        bool defeatedA, bool defeatedB, Shot& shot)
    {
        int damageA = attackB, damageB = attackA;
        if ((a.player && b.enemy && !defeatedB) || (b.player && a.enemy && !defeatedA))
        {
            int& damage = a.player ? damageB : damageA;
            damage *= shot.ConsumeBankShotDamageMultiplier();
            damage += shot.ConsumePlayerEnemyRelicDamageBonus();
        }
        if (a.enemy && b.enemy)
        {
            damageA += shot.GetCurrentShotCollisionAttackBonus();
            damageB += shot.GetCurrentShotCollisionAttackBonus();
        }
        return { damageA, damageB };
    }

    inline auto OrderingKey(const Body& body, int hp, int attack, const std::string& enemyId)
    {
        const auto& p = body.position;
        const auto& v = body.velocity;
        const auto& s = body.status;
        return std::make_tuple(!body.player, p.x, p.z, p.y, v.x, v.z, s.radius, s.mass,
            s.restitution, s.friction, hp, attack, enemyId);
    }
}
