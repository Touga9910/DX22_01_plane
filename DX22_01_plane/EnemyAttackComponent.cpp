#include "EnemyAttackComponent.h"

#include "EnemyBall.h"
#include "GameObject.h"
#include "PlayerBall.h"

#include <algorithm>
#include <cmath>

void EnemyAttackComponent::Awake()
{
    m_Enemy = GetGameObject()->GetComponent<EnemyBall>();
}

int EnemyAttackComponent::CalculateDamage(float damageMultiplier) const
{
    if (m_Enemy == nullptr || m_Enemy->IsDefeated())
    {
        return 0;
    }
    return (std::max)(0, static_cast<int>(std::lround(
        static_cast<float>(m_Enemy->GetAttack()) *
        (std::max)(0.0f, damageMultiplier))));
}

void EnemyAttackComponent::Attack(
    PlayerBall* player,
    float damageMultiplier)
{
    if (m_Enemy == nullptr || player == nullptr)
    {
        return;
    }

    if (m_Enemy->IsDefeated() || player->IsDefeated())
    {
        return;
    }

    player->TakeDamage(CalculateDamage(damageMultiplier));
}
