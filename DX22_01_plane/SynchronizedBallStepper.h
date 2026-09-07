#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

// All bodies cross each movement barrier together. The adapter owns game effects.
// The duration is a fraction of one legacy 60 Hz tick, not seconds.
namespace SynchronizedBallStepper
{
    inline constexpr int MaxSubsteps = 256;
    struct Result
    {
        int substeps = 0;
        double advancedFraction = 0.0;
        bool limitReached = false;
    };

    template<class Adapter>
    Result Step(Adapter& world)
    {
        Result result;
        if (world.Count() == 0) return result;
        double remaining = 1.0;
        while (remaining > 1.0e-9 && result.substeps < MaxSubsteps)
        {
            // Re-evaluate after every contact: heavy transfers can accelerate a ball.
            double maxSpeed = 0.0;
            double minRadius = (std::numeric_limits<double>::max)();
            for (std::size_t i = 0; i < world.Count(); ++i)
            {
                if (!world.IsActive(i)) continue;
                const double speed = world.Velocity(i).Length();
                const double radius = world.Radius(i);
                if (!std::isfinite(speed) || !std::isfinite(radius) || radius <= 0.0)
                {
                    result.limitReached = true;
                    return result;
                }
                maxSpeed = (std::max)(maxSpeed, speed);
                minRadius = (std::min)(minRadius, radius);
            }
            // Twice the largest speed bounds every pair's relative speed.
            // Invalid motion must never become an unchecked full-tick translation.
            if (!std::isfinite(maxSpeed) || minRadius <= 0.0)
            {
                result.limitReached = true;
                break;
            }
            const double interval = maxSpeed > 1.0e-8
                ? (std::min)(remaining, minRadius * 0.5 / (2.0 * maxSpeed))
                : remaining;
            if (!(interval > 0.0) || !std::isfinite(interval))
            {
                result.limitReached = true;
                break;
            }
            world.BeginSubstep();
            for (std::size_t i = 0; i < world.Count(); ++i)
                if (world.IsActive(i)) world.Move(i, static_cast<float>(interval));
            // No contact is evaluated until EVERY ball has advanced to this time.
            for (std::size_t i = 0; i < world.Count(); ++i)
                if (world.IsActive(i)) world.ResolveEnvironment(i);
            for (std::size_t i = 0; i < world.Count(); ++i)
            {
                for (std::size_t j = i + 1; j < world.Count(); ++j)
                {
                    if (!world.IsActive(i)) break;
                    if (world.IsActive(j)) world.ResolvePair(i, j);
                }
            }
            remaining -= interval;
            result.advancedFraction += interval;
            ++result.substeps;
            if (!world.ShouldContinue()) break;
        }
        result.limitReached = result.limitReached ||
            (result.substeps == MaxSubsteps && remaining > 1.0e-9);
        return result;
    }
}
