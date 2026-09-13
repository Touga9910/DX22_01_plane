#pragma once

#include "Collision.h"
#include <algorithm>
#include <cmath>
#include <limits>

// Times are fractions of one 60 Hz tick; positions/velocities retain game units.
namespace BallCcdGeometry
{
    using DirectX::SimpleMath::Vector3;
    inline constexpr float ContactSlop = 0.0001f;
    inline constexpr float ReleaseSlop = 0.001f;
    inline constexpr float ApproachEpsilon = 0.00001f;
    inline constexpr double NoHit = (std::numeric_limits<double>::infinity)();

    inline bool Finite(const Vector3& v)
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
    inline double Dot(const Vector3& a, const Vector3& b)
    {
        return static_cast<double>(a.x) * b.x + static_cast<double>(a.y) * b.y + static_cast<double>(a.z) * b.z;
    }

    inline double SphereTime(const Vector3& offset, const Vector3& velocity, double radius,
        double remaining, bool trigger = false)
    {
        const double lengthSq = Dot(offset, offset);
        const double speedSq = Dot(velocity, velocity);
        const double approach = Dot(offset, velocity);
        const double c = lengthSq - radius * radius;
        const double outer = radius + ContactSlop;
        const double inner = (std::max)(0.0, radius - ContactSlop);
        if (lengthSq <= outer * outer)
        {
            if (trigger || lengthSq < inner * inner || approach < -ApproachEpsilon * radius) return 0.0;
            return NoHit; // Touching but separating/tangent: do not repeat t=0.
        }
        if (speedSq <= 1.0e-20 || approach >= 0.0) return NoHit;
        const double discriminant = approach * approach - speedSq * c;
        if (discriminant < 0.0) return NoHit;
        // Equivalent to (-b - sqrt(b*b-a*c))/a without subtractive cancellation.
        const double time = c / (-approach + std::sqrt(discriminant));
        return time >= 0.0 && time <= remaining ? time : NoHit;
    }

    inline Vector3 ClosestXZ(const Vector3& point, const Collision::Segment& segment, float* projection = nullptr)
    {
        Vector3 direction = segment.end - segment.start;
        direction.y = 0;
        Vector3 offset = point - segment.start;
        offset.y = 0;
        const double denominator = Dot(direction, direction);
        const float t = denominator > 1.0e-20 ? static_cast<float>(Dot(offset, direction) / denominator) : 0.0f;
        if (projection) *projection = t;
        Vector3 closest = segment.start + direction * std::clamp(t, 0.0f, 1.0f);
        closest.y = point.y;
        return closest;
    }

    inline Vector3 InwardNormal(const Collision::Segment& wall, const Vector3& interior)
    {
        Vector3 direction = wall.end - wall.start;
        direction.y = 0;
        if (direction.LengthSquared() <= 1.0e-12f) return Vector3::UnitX;
        direction.Normalize();
        Vector3 normal(-direction.z, 0, direction.x);
        if ((interior - wall.start).Dot(normal) < 0) normal = -normal;
        return normal;
    }

    inline double WallTime(const Vector3& position, Vector3 velocity, float radius,
        const Collision::Segment& wall, double remaining)
    {
        velocity.y = 0;
        const Vector3 closest = ClosestXZ(position, wall);
        const Vector3 offset = position - closest;
        const double distance = std::sqrt(Dot(offset, offset));
        if (distance < radius - ContactSlop) return 0;
        if (distance <= radius + ContactSlop && Dot(offset, velocity) < -ApproachEpsilon * radius) return 0;

        Vector3 first = position - wall.start, second = position - wall.end;
        first.y = second.y = 0;
        double earliest = (std::min)(SphereTime(first, velocity, radius, remaining),
            SphereTime(second, velocity, radius, remaining));
        Vector3 direction = wall.end - wall.start;
        direction.y = 0;
        const double length = std::sqrt(Dot(direction, direction));
        if (length <= 1.0e-12) return earliest; // Degenerate segment is a point cap.
        direction /= static_cast<float>(length);
        const Vector3 normal(-direction.z, 0, direction.x);
        const double distanceToLine = Dot(first, normal);
        const double normalSpeed = Dot(velocity, normal);
        if (std::abs(normalSpeed) <= 1.0e-12) return earliest;
        for (const double side : { -1.0, 1.0 })
        {
            if (normalSpeed * side >= -ApproachEpsilon) continue;
            const double time = (side * radius - distanceToLine) / normalSpeed;
            if (time < 0 || time > remaining) continue;
            const double along = Dot(first, direction) + Dot(velocity, direction) * time;
            if (along >= 0 && along <= length) earliest = (std::min)(earliest, time);
        }
        return earliest;
    }

    inline double PocketTime(Vector3 position, Vector3 velocity, float radius,
        const Collision::Sphere& pocket, double remaining)
    {
        position -= pocket.center;
        position.y = velocity.y = 0;
        return SphereTime(position, velocity, static_cast<double>(radius) + pocket.radius, remaining, true);
    }
}
