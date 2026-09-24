#pragma once

#include "Component.h"

class EnemyBall;
class PlayerBall;

// EnemyBallの攻撃処理を担当するコンポーネント
// 所有GameObject上のEnemyBallを参照し、その攻撃力でPlayerBallへダメージを与える
class EnemyAttackComponent final : public Component
{
public:
    // 所有GameObjectからEnemyBallコンポーネントを取得して保持
    void Awake() override;

    // 指定したプレイヤーへ敵の攻撃力分のダメージを与える
    // 敵またはplayerがnullptr、またはどちらかが撃破済みの場合は何もしない
    void Attack(PlayerBall* player);

private:
    EnemyBall* m_Enemy = nullptr; // 同じGameObjectに付与されているEnemyBall。取得できない場合はnullptr
};
