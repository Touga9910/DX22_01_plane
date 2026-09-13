#include "EnemyAttackComponent.h"

#include "EnemyBall.h"
#include "GameObject.h"
#include "PlayerBall.h"

void EnemyAttackComponent::Awake()
{
    m_Enemy = GetGameObject()->GetComponent<EnemyBall>();
}

void EnemyAttackComponent::Attack(PlayerBall* player)
{
    if (m_Enemy == nullptr || player == nullptr)
    {
        return;
    }

    if (m_Enemy->IsDefeated() || player->IsDefeated())
    {
        return;
    }

    player->TakeDamage(m_Enemy->GetAttack());
}
