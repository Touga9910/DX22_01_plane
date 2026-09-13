#pragma once

#include "BallPhysicsRules.h"
#include "SynchronizedBallStepper.h"
#include <vector>

// One global earliest event, then recalculate from the resolved velocities.
// Physical contact episodes (owned by Body) are distinct from these iterations.
namespace ContinuousBallStepper
{
    using Result = SynchronizedBallStepper::Result;
    inline constexpr int MaxIterations = 256;

    inline bool Valid(const BallPhysicsRules::Body& body)
    {
        return BallCcdGeometry::Finite(body.position) && BallCcdGeometry::Finite(body.velocity) &&
            std::isfinite(body.status.radius) && body.status.radius > 0 &&
            std::isfinite(body.status.mass) && body.status.mass > 0;
    }

    template<class Adapter>
    void ResolveContacts(Adapter& world)
    {
        // Pockets take precedence over walls/pairs at an equal event time.
        for (std::size_t i = 0; i < world.Count(); ++i)
            if (world.IsActive(i)) world.ResolveEnvironment(i);
        for (std::size_t i = 0; i < world.Count(); ++i)
            for (std::size_t j = i + 1; j < world.Count(); ++j)
            {
                if (!world.IsActive(i)) break;
                if (world.IsActive(j)) world.ResolvePair(i, j);
            }
    }

    template<class Adapter>
    Result Step(Adapter& world)
    {
        Result result;
        if (world.Count() == 0) return result;
        for (std::size_t i = 0; i < world.Count(); ++i)
            if (world.IsActive(i) && !Valid(world.PhysicsBody(i))) { result.limitReached = true; return result; }
        for (const auto& wall : world.Walls())
            if (!BallCcdGeometry::Finite(wall.start) || !BallCcdGeometry::Finite(wall.end))
            { result.limitReached = true; return result; }
        for (const auto& pocket : world.PocketSpheres())
            if (!BallCcdGeometry::Finite(pocket.center) || !std::isfinite(pocket.radius) || pocket.radius < 0)
            { result.limitReached = true; return result; }
        // Recover starting overlaps and retire starting pocket overlaps without
        // advancing time. Damage still requires approaching relative motion.
        world.BeginSubstep();
        ResolveContacts(world);
        if (!world.ShouldContinue()) return result;

        double remaining = 1.0;
        std::vector<BallPhysicsRules::Body> bodies;
        std::vector<bool> active;
        while (remaining > 1.0e-9 && result.substeps < MaxIterations)
        {
            // Retain each body's contact-vector capacity between TOI iterations.
            bodies.resize(world.Count());
            active.resize(world.Count());
            for (std::size_t i = 0; i < world.Count(); ++i)
            {
                bodies[i] = world.PhysicsBody(i);
                active[i] = world.IsActive(i);
                const auto& b = bodies[i];
                if (active[i] && !Valid(b))
                {
                    result.limitReached = true;
                    return result;
                }
            }
            double earliest = remaining;
            for (std::size_t i = 0; i < bodies.size(); ++i)
            {
                if (!active[i]) continue;
                const auto& a = bodies[i];
                if (!a.boss) for (const auto& pocket : world.PocketSpheres())
                    earliest = (std::min)(earliest, BallCcdGeometry::PocketTime(a.position, a.velocity,
                        a.status.radius, pocket, remaining));
                if (a.boss) for (const auto& wall : BallPhysicsRules::BossWalls())
                    earliest = (std::min)(earliest, BallCcdGeometry::WallTime(a.position, a.velocity,
                        a.status.radius, wall, remaining));
                for (const auto& wall : world.Walls())
                    earliest = (std::min)(earliest, BallCcdGeometry::WallTime(a.position, a.velocity,
                        a.status.radius, wall, remaining));
                for (std::size_t j = i + 1; j < bodies.size(); ++j)
                {
                    if (!active[j]) continue;
                    const auto& b = bodies[j];
                    if (BallPhysicsRules::IgnoresPiercedPair(a, b)) continue;
                    earliest = (std::min)(earliest, BallCcdGeometry::SphereTime(a.position - b.position,
                        a.velocity - b.velocity, static_cast<double>(a.status.radius) + b.status.radius, remaining));
                }
            }
            world.BeginSubstep();
            for (std::size_t i = 0; i < world.Count(); ++i)
                if (world.IsActive(i)) world.Move(i, static_cast<float>(earliest));
            ResolveContacts(world);
            remaining -= earliest;
            result.advancedFraction += earliest;
            ++result.substeps;
            if (!world.ShouldContinue()) return result;
        }
        // Never translate through unchecked geometry when a contact cluster fails
        // to converge. Live telemetry and preview completeness expose this limit.
        result.limitReached = remaining > 1.0e-9;
        return result;
    }
}
