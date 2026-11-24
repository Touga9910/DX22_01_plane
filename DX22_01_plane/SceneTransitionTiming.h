#pragma once
#include <algorithm>
#include <cmath>

struct SceneTransitionTiming
{
    static constexpr float MaximumSeconds = 3.0f;
    float fadeOutSeconds = .55f;
    float blackHoldSeconds = .5f;
    float fadeInSeconds = .75f;

    /// <summary>
    /// フェード時間を有効な範囲に収め、有限値でなければ既定値を返す。
    /// </summary>
    static float ClampSeconds(float value, float fallback)
    {
        return std::isfinite(value) ? std::clamp(value, 0.0f, MaximumSeconds) : fallback;
    }
    /// <summary>
    /// フェードアウト、暗転とフェードインの時間を設定可能な範囲に正規化する。
    /// </summary>
    void Normalize()
    {
        const SceneTransitionTiming defaults;
        fadeOutSeconds = ClampSeconds(fadeOutSeconds, defaults.fadeOutSeconds);
        blackHoldSeconds = ClampSeconds(blackHoldSeconds, defaults.blackHoldSeconds);
        fadeInSeconds = ClampSeconds(fadeInSeconds, defaults.fadeInSeconds);
    }
};
