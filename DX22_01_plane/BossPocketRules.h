#pragma once

#include "PocketReturnRules.h"
#include "Collision.h"
#include <optional>
#include <vector>

// Identical deterministic placement for live physics and disposable predictions.
namespace BossPocketRules
{
    using DirectX::SimpleMath::Vector3;
    struct Obstacle { Vector3 position; float radius; };

    inline bool IsSafePosition(const Vector3& p, float radius,
        const std::vector<Obstacle>& obstacles, const std::vector<Collision::Sphere>& pockets)
    {
            if (std::abs(p.x) + radius + 0.5f > TableConfig::GetFieldWidth() * 0.5f ||
                std::abs(p.z) + radius + 0.5f > TableConfig::GetFieldDepth() * 0.5f) return false;
            for (const auto& other : obstacles)
            {
                auto delta = p - other.position; delta.y = 0;
                const float clearance = radius + other.radius + 0.5f;
                if (delta.LengthSquared() < clearance * clearance) return false;
            }
            for (const auto& pocket : pockets)
            {
                auto delta = p - pocket.center; delta.y = 0;
                const float clearance = radius + pocket.radius + 0.5f;
                if (delta.LengthSquared() < clearance * clearance) return false;
            }
            return true;
    }

    // Spawning honors authored world coordinates, unlike a pocket return anchor.
    // Local fallback preserves the formation when a custom layout blocks a slot.
    inline std::optional<Vector3> FindSpawnPosition(const Vector3& preferred, float radius,
        const std::vector<Obstacle>& obstacles, const std::vector<Collision::Sphere>& pockets)
    {
        if (IsSafePosition(preferred,radius,obstacles,pockets)) return preferred;
        for (float distance : {8.0f,16.0f,24.0f})
            for (const Vector3& direction : {Vector3(-1,0,0),Vector3(1,0,0),Vector3(0,0,-1),Vector3(0,0,1),
                                           Vector3(-1,0,-1),Vector3(1,0,-1),Vector3(-1,0,1),Vector3(1,0,1)})
            {
                auto p = preferred+direction*distance;
                p.y = TableConfig::FIELD_HEIGHT;
                if (IsSafePosition(p,radius,obstacles,pockets)) return p;
            }
        return std::nullopt;
    }

    inline std::optional<Vector3> FindReturnPosition(const Vector3& entry, float radius,
        const std::vector<Obstacle>& obstacles, const std::vector<Collision::Sphere>& pockets)
    {
        const auto safe = [&](const Vector3& p) { return IsSafePosition(p,radius,obstacles,pockets); };
        const auto inward = PocketReturnRules::InwardDirection(entry);
        const Vector3 side(-inward.z, 0, inward.x);
        // Prefer the same side of the table; fallback search never forces overlap.
        for (float distance : {radius + 12.0f, radius + 20.0f, radius + 28.0f})
            for (float offset : {0.0f, 12.0f, -12.0f, 24.0f, -24.0f})
            {
                const auto p = PocketReturnRules::ReturnAnchor(entry, distance) + side * offset;
                if (safe(p)) return p;
            }
        for (float z = -24; z <= 24; z += 8)
            for (float x = -56; x <= 56; x += 8)
            {
                const Vector3 p(x, TableConfig::FIELD_HEIGHT, z);
                if (safe(p)) return p;
            }
        return std::nullopt;
    }
}
