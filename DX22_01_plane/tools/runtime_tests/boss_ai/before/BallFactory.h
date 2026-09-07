#pragma once

class EnemyBall;
class BreakBall;
class Game;
class PlayerBall;
struct EnemyData;

class BallFactory final
{
public:
    static PlayerBall* CreatePlayer(Game& game);
    static BreakBall* CreateBreakBall(Game& game, int index);
    static EnemyBall* CreateEnemy(Game& game, const EnemyData& data);
};
