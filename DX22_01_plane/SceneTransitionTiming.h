#pragma once
#include <algorithm>
#include <cmath>

struct SceneTransitionTiming
{
    static constexpr float MaximumSeconds = 3.0f;
    float fadeOutSeconds = .55f;
    float blackHoldSeconds = .5f;
    float fadeInSeconds = .75f;

    static float ClampSeconds(float value, float fallback)
    {
        return std::isfinite(value) ? std::clamp(value, 0.0f, MaximumSeconds) : fallback;
    }
    void Normalize()
    {
        const SceneTransitionTiming defaults;
        fadeOutSeconds = ClampSeconds(fadeOutSeconds, defaults.fadeOutSeconds);
        blackHoldSeconds = ClampSeconds(blackHoldSeconds, defaults.blackHoldSeconds);
        fadeInSeconds = ClampSeconds(fadeInSeconds, defaults.fadeInSeconds);
    }
};
