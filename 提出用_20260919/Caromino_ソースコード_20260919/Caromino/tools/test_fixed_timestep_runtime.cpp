#include "../FixedStepClock.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

int main()
{
    for (int hz : {30, 60, 144})
    {
        FixedStepClock clock;
        int ticks = 0;
        for (int frame = 0; frame < hz * 10; ++frame)
            ticks += clock.Advance(1.0 / hz);
        assert(ticks == 600);
        assert(clock.RemainderSeconds() < 1.0e-8);
        assert(clock.DroppedSeconds() == 0.0);
    }
    // Irregular display intervals must retain fractional time, not round each frame.
    FixedStepClock irregular;
    int ticks = 0;
    for (int i = 0; i < 100; ++i)
        for (double elapsed : {0.007, 0.003, 0.020, 0.010, 0.060})
            ticks += irregular.Advance(elapsed);
    assert(ticks == 600);

    FixedStepClock stall;
    assert(stall.Advance(2.0) == 8);
    assert(std::abs(stall.DroppedSeconds() - (2.0 - 8.0 / 60.0)) < 1.0e-8);
    assert(stall.Advance(FixedStepClock::StepSeconds) == 1);
    assert(stall.Advance(-1.0) == 0);
    assert(stall.Advance(std::numeric_limits<double>::infinity()) == 0);
    assert(stall.Advance(std::numeric_limits<double>::quiet_NaN()) == 0);

    // No stale fractional time crosses pause, scene or shot boundaries.
    FixedStepClock reset;
    assert(reset.Advance(0.01) == 0);
    reset.Reset();
    assert(reset.Advance(0.01) == 0);
    assert(reset.Advance(0.01) == 1);
    assert(reset.Advance(0.0) == 0);

    // The 11-tick settling interval has equal physical duration at every render rate.
    for (int hz : {30, 60, 144})
    {
        FixedStepClock settling;
        int stoppedTicks = 0;
        int frame = 0;
        while (stoppedTicks < 11)
        {
            ++frame;
            int due = settling.Advance(1.0 / hz);
            for (int i = 0; i < due && stoppedTicks < 11; ++i) ++stoppedTicks;
        }
        assert(stoppedTicks == 11);
        assert(static_cast<double>(frame) / hz >= 11.0 / 60.0 - 1.0e-9);
        assert(static_cast<double>(frame) / hz < 11.0 / 60.0 + 1.0 / hz + 1.0e-9);
    }
    std::cout << "Fixed clock: 30/60/144 Hz, irregular frames, stall, reset, settling passed\n";
}
