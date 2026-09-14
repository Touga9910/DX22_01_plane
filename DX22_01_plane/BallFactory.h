#pragma once

#include "SimpleMath.h"

class EnemyBall;
class BreakBall;
class Game;
class PlayerBall;
class NuisanceBall;
struct EnemyData;
struct NuisanceBallData;

class BallFactory final
{
public:
    static PlayerBall* CreatePlayer(Game& game);
    static BreakBall* CreateBreakBall(Game& game, int index);
    static EnemyBall* CreateEnemy(Game& game, const EnemyData& data);
    static NuisanceBall* CreateNuisanceBall(
        Game& game,
        const NuisanceBallData& data,
        const DirectX::SimpleMath::Vector3& position);
};
