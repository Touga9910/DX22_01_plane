#pragma once

#include "ContinuousBallStepper.h"

#include <functional>

class BallComponent;
class GameWorld;

namespace DirectX::SimpleMath
{
    struct Vector3;
}

struct BallPhysicsWorldContext
{
    GameWorld& world;
    std::function<bool()> shouldContinue;
    std::function<void(
        BallComponent&,
        const DirectX::SimpleMath::Vector3&,
        const DirectX::SimpleMath::Vector3&)> traceMovement;
};

class BallPhysicsWorld final
{
public:
    static constexpr const char* ModelName = "synchronized_ccd_toi_v1";
    // Call once after all components have prepared friction/stop state for a tick.
    static ContinuousBallStepper::Result Step(
        const BallPhysicsWorldContext& context);
};
