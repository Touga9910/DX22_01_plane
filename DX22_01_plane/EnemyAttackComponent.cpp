#include "EnemyAttackComponent.h"

#include "EnemyBall.h"
#include "GameObject.h"
#include "PlayerBall.h"
#include "BalanceLogger.h"

#include <algorithm>
#include <cmath>

void EnemyAttackComponent::Awake()
{
    m_Enemy = GetGameObject()->GetComponent<EnemyBall>();
}

int EnemyAttackComponent::CalculateDamage(float damageMultiplier) const
{
    if (m_Enemy == nullptr || !m_Enemy->CanParticipateInCombat())
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

    if (!m_Enemy->CanParticipateInCombat() || player->IsDefeated())
    {
        return;
    }

	const int resolvedDamage = CalculateDamage(damageMultiplier);
	if (m_Enemy->IsBrittle())
	{
		const int normalDamage = PlayerBall::CalculateIncomingDamage(
			(std::max)(0, static_cast<int>(std::lround(
				static_cast<float>(m_Enemy->GetAttackBeforeBoardAbility()) *
				(std::max)(0.0f, damageMultiplier)))));
		const int brittleDamage =
			PlayerBall::CalculateIncomingDamage(resolvedDamage);
		BalanceLogger::GetInstance().RecordEvent(
			"enemy_brittle_player_damage_reduced",
			{
				{ "enemy_id", m_Enemy->GetEnemyId() },
				{ "prevented_damage", (std::max)(0, normalDamage - brittleDamage) },
				{ "normal_damage", normalDamage },
				{ "brittle_damage", brittleDamage },
			});
	}
    player->TakeDamage(resolvedDamage);
	m_Enemy->ConsumeBoardAbilityAfterAttack();
}
