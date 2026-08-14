#pragma once

#include "SimpleMath.h"

class Game;
class Pocket;

class PocketFactory final
{
public:
    static Pocket* Create(
        Game& game,
        const DirectX::SimpleMath::Vector3& position,
        float radius = 2.0f);
};
