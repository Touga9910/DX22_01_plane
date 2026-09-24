#pragma once

#include <algorithm>
#include <cmath>

// One tick retains the legacy velocity/friction units (distance per 1/60 s).
// This schedules physics only; input, commands and turn actions stay outside.
class FixedStepClock final
{
public:
    static constexpr double StepSeconds = 1.0 / 60.0;
    static constexpr int MaxStepsPerFrame = 8;
    static constexpr double MaxFrameSeconds = 0.25;

    int Advance(double elapsedSeconds)
    {
        if (!std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.0)
        {
            return 0;
        }
        const double accepted = (std::min)(elapsedSeconds, MaxFrameSeconds);
        m_DroppedSeconds += elapsedSeconds - accepted;
        m_RemainderSeconds += accepted;
        // Avoid losing an exact tick to roundoff (e.g. 144 Hz for one second).
        const int due = static_cast<int>(
            std::floor((m_RemainderSeconds + 1.0e-10) / StepSeconds));
        m_RemainderSeconds = (std::max)(0.0,
            m_RemainderSeconds - due * StepSeconds);
        const int steps = (std::min)(due, MaxStepsPerFrame);
        m_DroppedSeconds += (due - steps) * StepSeconds;
        return steps;
    }

    // Pauses, scene changes and new shots must not inherit old time debt.
    void Reset() { m_RemainderSeconds = 0.0; }
    double RemainderSeconds() const { return m_RemainderSeconds; }
    double DroppedSeconds() const { return m_DroppedSeconds; }

private:
    double m_RemainderSeconds = 0.0;
    double m_DroppedSeconds = 0.0;
};
