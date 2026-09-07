#pragma once

#include "BallMechanics.h"
#include "Collision.h"
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
        bool player = false, enemy = false;
        int pierceUses = 0, pierceLimit = 0;
        float pierceRetention = 0.75f;
        std::vector<std::uintptr_t> pierced;
    };

    inline bool PlayerFriction(Vector3& velocity, Vector3& acceleration,
        const BallStatus& status, float friction, int& stopCount)
    {
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
        const float trigger = radius + pocket.radius;
        return Collision::DistanceSquaredPointToSegment(pocket.center, { start, end }) <= trigger * trigger;
    }

    inline bool Wall(Body& body, const Collision::Segment& wall, const Vector3& interior)
    {
        Vector3 contact;
        if (Collision::DistancePointToSegment(body.position, wall, contact) > body.status.radius) return false;
        Vector3 direction = wall.end - wall.start;
        direction.y = 0.0f;
        if (direction.LengthSquared() <= 0.0001f) return false;
        direction.Normalize();
        Vector3 normal(-direction.z, 0.0f, direction.x);
        if (Collision::Dot(interior - contact, normal) < 0.0f) normal = -normal;
        body.position = contact + normal * body.status.radius;
        const float dot = Collision::Dot(body.velocity, normal);
        if (dot >= 0.0f) return false;
        body.velocity = (body.velocity - normal * (2.0f * dot)) * body.status.restitution;
        return true;
    }

    inline bool AnchorLocked(const Body& body)
    {
        return body.player && body.status.abilities.anchor && body.status.anchorKnockbackImmune &&
            body.velocity.LengthSquared() <= 0.0001f;
    }

    // True means an approaching contact (one attack event); separation also runs on false.
    inline bool Pair(Body& a, Body& b)
    {
        Vector3 normal = a.position - b.position;
        const float distance = normal.Length();
        const float separation = a.status.radius + b.status.radius;
        if (distance >= separation)
        {
            std::erase(a.pierced, b.id);
            std::erase(b.pierced, a.id);
            return false;
        }
        if (std::find(a.pierced.begin(), a.pierced.end(), b.id) != a.pierced.end() ||
            std::find(b.pierced.begin(), b.pierced.end(), a.id) != b.pierced.end()) return false;
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
            const float overlap = separation - distance;
            if (!aLocked) a.position += normal * overlap * (bLocked ? 1.0f : 0.5f);
            if (!bLocked) b.position += -normal * overlap * (aLocked ? 1.0f : 0.5f);
        }
        const float av = Collision::Dot(a.velocity, normal), bv = Collision::Dot(b.velocity, normal);
        if (av - bv >= 0.0f) return false;
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
                restitution, a.player && b.enemy ? a.status.knockbackTransfer : 1.0f,
                b.player && a.enemy ? b.status.knockbackTransfer : 1.0f, aLocked, bLocked);
            a.velocity += normal * (response.first - av);
            b.velocity += normal * (response.second - bv);
        }
        return true;
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
