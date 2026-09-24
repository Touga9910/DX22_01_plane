#pragma once

#include "SimpleMath.h"

class EnemyBall;
class BreakBall;
class Game;
class PlayerBall;
class NuisanceBall;
struct EnemyData;
struct NuisanceBallData;

// プレイヤー・敵・ブレイク・お邪魔ボール用GameObjectを共通構成で生成するFactory
// 生成されるボールには状態・物理・コライダー・描画・衝突・BallComponentが共通で追加される
class BallFactory final
{
public:
    // Playerタグを持つボールGameObjectを生成し、追加したPlayerBallを返す
    static PlayerBall* CreatePlayer(Game& game);

    // BreakBall用の共通ボールGameObjectを生成し、指定indexを持つBreakBallを返す
    static BreakBall* CreateBreakBall(Game& game, int index);

    // Enemyタグを持つボールGameObjectを生成し、EnemyDataを渡したEnemyBallと攻撃Componentを追加して返す
    static EnemyBall* CreateEnemy(Game& game, const EnemyData& data);

    // お邪魔ボール用の共通GameObjectを生成し、設定データと初期位置を渡したNuisanceBallを返す
    static NuisanceBall* CreateNuisanceBall(
        Game& game,
        const NuisanceBallData& data,
        const DirectX::SimpleMath::Vector3& position);
};
