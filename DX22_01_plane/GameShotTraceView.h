#pragma once

#include "PierceTraceRules.h"

// Read-only view of the active shot traces used by player-ball rendering.
// The heavy trace state stays out of the general battle view boundary.
class GameShotTraceView final
{
public:
    static const PierceTraceRules::State& Current();

    GameShotTraceView() = delete;
};
