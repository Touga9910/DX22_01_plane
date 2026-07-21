#pragma once

class EnemyBall;
class Game;
class PlayerBall;
struct EnemyData;

class BallFactory final
{
public:
    static PlayerBall* CreatePlayer(Game& game);
    static EnemyBall* CreateEnemy(Game& game, const EnemyData& data);
};
