#pragma once

#include "ContinuousBallStepper.h"

class Game;

class BallPhysicsWorld final
{
public:
    static constexpr const char* ModelName = "synchronized_ccd_toi_v1";
    // Call once after all components have prepared friction/stop state for a tick.
    static ContinuousBallStepper::Result Step(Game& game);
};
