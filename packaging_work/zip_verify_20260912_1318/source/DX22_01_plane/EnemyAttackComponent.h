#pragma once

#include "Component.h"

class EnemyBall;
class PlayerBall;

class EnemyAttackComponent final : public Component
{
public:
    void Awake() override;
    void Attack(PlayerBall* player);

private:
    EnemyBall* m_Enemy = nullptr;
};
