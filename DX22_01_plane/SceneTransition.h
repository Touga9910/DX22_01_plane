#pragma once
#include "SceneTransitionTiming.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

// Visual-time state machine. Scene mutation only happens after a full black
// frame has been presented; the caller must freeze gameplay while Active().
class SceneTransition
{
public:
    enum class Phase { Idle, FadeOut, Black, FadeIn };
    void Configure(SceneTransitionTiming timing)
    {
        if (Active()) return;
        timing.Normalize();
        m_Timing = timing;
    }

    bool Request(std::function<void()> action, bool alreadyBlack = false)
    {
        if (Active() || !action) return false;
        m_Preview = false;
        m_Action = std::move(action);
        m_Phase = alreadyBlack ? Phase::Black : Phase::FadeOut;
        m_Time = 0;
        return true;
    }
    bool RequestPreview(SceneTransitionTiming timing)
    {
        if (Active()) return false;
        Configure(timing);
        if (!Request([] {})) return false;
        m_Preview = true;
        return true;
    }
    bool IsPreviewing() const { return m_Preview && Active(); }
    bool CancelPreview()
    {
        if (!IsPreviewing()) return false;
        Reset();
        return true;
    }
    void BeginFadeIn() { m_Phase = Phase::FadeIn; m_Time = 0; }
    void Reset() { m_Action = {}; m_Phase = Phase::Idle; m_Time = 0; m_Committing = false; m_Preview = false; }
    bool Active() const { return m_Phase != Phase::Idle; }
    bool IsCommitting() const { return m_Committing; }
    Phase GetPhase() const { return m_Phase; }
    float Alpha() const
    {
        if (m_Phase == Phase::Idle) return 0;
        if (m_Phase == Phase::Black) return 1;
        const float duration = m_Phase == Phase::FadeOut ? m_Timing.fadeOutSeconds : m_Timing.fadeInSeconds;
        const float t = duration > 0 ? std::clamp(m_Time / duration, 0.0f, 1.0f) : 1;
        const float eased = t * t * (3 - 2 * t);
        return m_Phase == Phase::FadeOut ? eased : 1 - eased;
    }
    // True also on the final fade-in frame: no input/physics catches up then.
    bool Advance(double seconds)
    {
        if (!Active()) return false;
        const float dt = std::isfinite(seconds) ? static_cast<float>(std::clamp(seconds, 0.0, .1)) : 0;
        if (m_Phase == Phase::Black)
        {
            m_Time += dt;
            if (m_Time < m_Timing.blackHoldSeconds) return true;
            auto action = std::move(m_Action);
            m_Committing = true;
            try { if (action) action(); }
            catch (...) { m_Committing = false; BeginFadeIn(); throw; }
            m_Committing = false;
            BeginFadeIn();
            return true;
        }
        m_Time += dt;
        if (m_Phase == Phase::FadeOut && m_Time >= m_Timing.fadeOutSeconds)
        {
            m_Phase = Phase::Black; m_Time = 0;
        }
        else if (m_Phase == Phase::FadeIn && m_Time >= m_Timing.fadeInSeconds)
        {
            m_Phase = Phase::Idle; m_Time = 0; m_Preview = false;
        }
        return true;
    }
private:
    SceneTransitionTiming m_Timing;
    Phase m_Phase = Phase::Idle;
    float m_Time = 0;
    bool m_Committing = false;
    bool m_Preview = false;
    std::function<void()> m_Action;
};
