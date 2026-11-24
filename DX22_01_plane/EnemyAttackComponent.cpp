#include "EnemyAttackComponent.h"

#include "EnemyBall.h"
#include "GameObject.h"
#include "PlayerBall.h"
#include "BalanceLogger.h"

#include <algorithm>
#include <cmath>

/// <summary>
/// 所有オブジェクトから、攻撃元になる敵球コンポーネントを取得する。
/// </summary>
void EnemyAttackComponent::Awake()
{
    m_Enemy = GetGameObject()->GetComponent<EnemyBall>();
}

/// <summary>
/// 敵が戦闘に参加できる場合に、装甲ボスの行動や指定倍率を反映した攻撃ダメージを求める。
/// </summary>
int EnemyAttackComponent::CalculateDamage(float damageMultiplier) const
{
    if (m_Enemy == nullptr || !m_Enemy->CanParticipateInCombat())
    {
        return 0;
    }
    if (m_Enemy->IsArmorBoss()) return BossEncounterRules::BossDamage(m_Enemy->BossTurn(),m_Enemy->GetBossState());
    return (std::max)(0, static_cast<int>(std::lround(
        static_cast<float>(m_Enemy->GetAttack()) *
        (std::max)(0.0f, damageMultiplier))));
}

/// <summary>
/// 敵とプレイヤーの戦闘状態を確認し、計算したダメージをプレイヤーに適用する。
/// </summary>
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
