#pragma once

#include "SynchronizedBallStepper.h"

class Game;

class BallPhysicsWorld final
{
public:
    static constexpr const char* ModelName = "synchronized_discrete_v1";
    // Call once after all components have prepared friction/stop state for a tick.
    static SynchronizedBallStepper::Result Step(Game& game);
};
