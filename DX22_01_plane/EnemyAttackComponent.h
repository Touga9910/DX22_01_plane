#pragma once

#include "Component.h"

class EnemyBall;
class PlayerBall;

// Applies an EnemyBall's existing attack value to a PlayerBall.
class EnemyAttackComponent final : public Component
{
public:
	// Cache the EnemyBall attached to the same GameObject.
    void Awake() override;

	// Apply the current attack value with an optional action multiplier.
    void Attack(PlayerBall* player, float damageMultiplier = 1.0f);
    int CalculateDamage(float damageMultiplier = 1.0f) const;

private:
	EnemyBall* m_Enemy = nullptr;
};
