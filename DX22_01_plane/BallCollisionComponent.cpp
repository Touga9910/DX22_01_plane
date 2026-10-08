#include "BallCollisionComponent.h"

#include "BalanceLogger.h"
#include "BallComponent.h"
#include "BallMechanics.h"
#include "BallPhysicsRules.h"
#include "BallPhysicsComponent.h"
#include "EnemyBall.h"
#include "BreakBall.h"
#include "GameBattleCommands.h"
#include "GameBattleEvents.h"
#include "GameBattleView.h"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "GameObject.h"
#include "PlayerBall.h"
#include "Pocket.h"
#include "TableFrame.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace DirectX::SimpleMath;

namespace
{
    struct LiveShotRules final
    {
        int ConsumeBankShotDamageMultiplier()
        {
            return GameBattleCommands::ConsumeBankShotDamageMultiplier();
        }

        int ConsumePlayerEnemyRelicDamageBonus()
        {
            return GameBattleCommands::ConsumePlayerEnemyRelicDamageBonus();
        }

        int GetCurrentShotCollisionAttackBonus() const
        {
            return GameBattleView::CurrentShotCollisionAttackBonus();
        }
    };
}

void BallCollisionComponent::Awake()
{
	GameObject* owner = GetGameObject();
	if (owner == nullptr)
	{
		return;
	}

	m_PhysicsComponent = owner->GetComponent<BallPhysicsComponent>();
	if (m_PhysicsComponent == nullptr)
	{
		owner->Destroy();
	}
}

void BallCollisionComponent::BindBall(BallComponent& ball)
{
	m_BallComponent = &ball;
}

bool BallCollisionComponent::CanSimulate() const
{
	const GameObject* owner = GetGameObject();
	if (owner == nullptr || !owner->IsActive() || owner->IsDestroyRequested() ||
		!IsEnabled() || m_BallComponent == nullptr || !m_BallComponent->IsEnabled() ||
		m_PhysicsComponent == nullptr || !m_PhysicsComponent->IsEnabled()) return false;
	if (const auto* player = owner->GetComponent<PlayerBall>())
		return player->IsEnabled() && !player->IsDefeated() && !player->IsPocketed();
	if (const auto* enemy = owner->GetComponent<EnemyBall>())
		return enemy->IsEnabled() && !enemy->IsPocketed();
	return true;
}

void BallCollisionComponent::ForgetMissingContacts(const std::vector<BallComponent*>& activeBalls)
{
    const auto missing = [&activeBalls](const BallComponent* other) {
        return std::find(activeBalls.begin(), activeBalls.end(), other) == activeBalls.end();
    };
    std::erase_if(m_PiercedBalls, missing);
    std::erase_if(m_TouchingBalls, missing);
}

void BallCollisionComponent::ResolveEnvironment(
	const Vector3& movementStart,
	const std::vector<Collision::Segment>& walls,
	const Vector3& interiorReference,
	const std::vector<Pocket*>& pockets)
{
	if (!CanSimulate() || CheckPocketHitAlongMovement(movementStart, pockets)) return;
    auto body = CapturePhysicsBody();
    if (body.boss)
    {
        for (const auto& wall : BallPhysicsRules::BossWalls()) BallPhysicsRules::Wall(body, wall, Vector3::Zero);
    }
    for (const auto& wall : walls)
    {
		const bool bounced = BallPhysicsRules::Wall(body, wall, interiorReference);
        if (bounced && body.player)
        {
			GameBattleEvents::PlayerWallCollision(0, body.position, body.velocity);
        }
    }
    // Walls only change motion; rebuilding unchanged contact histories for each
    // wall copied the same vectors repeatedly during every TOI iteration.
    SetPosition(body.position);
    m_PhysicsComponent->Velocity() = body.velocity;
	if (CanSimulate()) CheckPocketHitAlongMovement(movementStart, pockets);
}

bool BallCollisionComponent::TryGetPierceExitDirection(Vector3& direction) const
{
    if (m_PiercedBalls.empty() || !CanSimulate()) return false;
    const auto player = CapturePhysicsBody();
    // Enumerate live components before dereferencing remembered contacts: a
    // remembered pointer may belong to an object removed since the last tick.
    for (const auto* other :
        GameRuntime::World().GetComponents<BallCollisionComponent>())
        if (other && other != this &&
            std::find(m_PiercedBalls.begin(), m_PiercedBalls.end(), other->m_BallComponent) != m_PiercedBalls.end() &&
            other->CanSimulate() && BallPhysicsRules::PierceExitDirection(player, other->CapturePhysicsBody(), direction))
            return true;
    return false;
}

void BallCollisionComponent::ResolveBallPair(BallCollisionComponent& otherContact)
{
	if (&otherContact == this || !CanSimulate() || !otherContact.CanSimulate()) return;
	BallCollisionComponent* otherCollision = &otherContact;
	BallComponent* other = otherCollision->m_BallComponent;
    auto first = CapturePhysicsBody();
    auto second = otherContact.CapturePhysicsBody();
	const Vector3 firstVelocityBefore = first.velocity;
	const Vector3 secondVelocityBefore = second.velocity;
	const int firstPierceUsesBefore = first.pierceUses;
	const int secondPierceUsesBefore = second.pierceUses;
    const bool impact = BallPhysicsRules::Pair(first, second);
    CommitPhysicsBody(first);
    otherContact.CommitPhysicsBody(second);
	if (!impact || GameBattleView::State() != BattleState::BallsMoving) return;
    auto* myEnemy = GetGameObject()->GetComponent<EnemyBall>();
    auto* otherEnemy = other->GetGameObject()->GetComponent<EnemyBall>();
    auto* myPlayer = GetGameObject()->GetComponent<PlayerBall>();
    auto* otherPlayer = other->GetGameObject()->GetComponent<PlayerBall>();
	const auto directionChanged = [](const Vector3& before, const Vector3& after)
	{
		const float lengths = before.Length() * after.Length();
		return lengths > 0.0001f && before.Dot(after) / lengths < 0.9999f;
	};
	if (myPlayer != nullptr && first.pierceUses == firstPierceUsesBefore &&
		directionChanged(firstVelocityBefore, first.velocity))
		GameBattleEvents::PlayerDirectionChange(first.position);
	if (otherPlayer != nullptr && second.pierceUses == secondPierceUsesBefore &&
		directionChanged(secondVelocityBefore, second.velocity))
		GameBattleEvents::PlayerDirectionChange(second.position);
    // Neutral contacts do not deal direct contact damage or consume damage relics.
    // A player's chain-impact ability may still use the neutral as its blast center.
    if (first.breakBall || second.breakBall)
    {
        const Vector3 firstPosition = first.position;
        const Vector3 secondPosition = second.position;
        if (first.breakBall && otherEnemy) GetGameObject()->GetComponent<BreakBall>()->HitBoss(*otherEnemy);
        if (second.breakBall && myEnemy) other->GetGameObject()->GetComponent<BreakBall>()->HitBoss(*myEnemy);
        if (first.breakBall && otherPlayer)
            GameBattleEvents::PlayerChainImpact(
                firstPosition, nullptr, other->GetAttack(), otherPlayer->GetStatus().chainImpactRadius);
        if (second.breakBall && myPlayer)
            GameBattleEvents::PlayerChainImpact(
                secondPosition, nullptr, GetAttack(), myPlayer->GetStatus().chainImpactRadius);
        if (BallPhysicsRules::StopAnchor(first, m_PhysicsComponent->Acceleration()))
        {
            CommitPhysicsBody(first);
            GameBattleEvents::AnchorStopped();
        }
        if (BallPhysicsRules::StopAnchor(second, otherCollision->m_PhysicsComponent->Acceleration()))
        {
            otherContact.CommitPhysicsBody(second);
            GameBattleEvents::AnchorStopped();
        }
        return;
    }
    const bool isPlayerEnemyCollision = (myPlayer && otherEnemy) || (myEnemy && otherPlayer);
    const bool isEnemyEnemyCollision = myEnemy && otherEnemy;
	if (isEnemyEnemyCollision)
	{
		GameBattleEvents::EnemyEnemySynergyCollision(
			myEnemy, otherEnemy, firstVelocityBefore, secondVelocityBefore);
	}
	if (isPlayerEnemyCollision)
	{
		EnemyBall* target = myEnemy != nullptr ? myEnemy : otherEnemy;
		const bool playerPierced =
			(myPlayer != nullptr && first.pierceUses > firstPierceUsesBefore) ||
			(otherPlayer != nullptr && second.pierceUses > secondPierceUsesBefore);
		if (playerPierced)
		{
			const BallPhysicsRules::Body& playerBody = myPlayer != nullptr ? first : second;
			GameBattleEvents::PlayerPiercedEnemy(
				target,
				playerBody.position,
				playerBody.velocity);
		}
	}
    LiveShotRules liveShotRules;
    auto damage = BallPhysicsRules::ContactDamage(first, second, GetAttack(), other->GetAttack(),
        myEnemy && myEnemy->IsDefeated(), otherEnemy && otherEnemy->IsDefeated(), liveShotRules);
	int damageToThis = damage.first, damageToOther = damage.second;
	if (isEnemyEnemyCollision)
	{
		damageToThis = GameBattleView::ScaleEnemyCollisionDamage(damageToThis);
		damageToOther = GameBattleView::ScaleEnemyCollisionDamage(damageToOther);
	}
	if (isPlayerEnemyCollision)
	{
		const int synergyBonus = GameBattleEvents::PlayerEnemySynergyCollision(
			myEnemy != nullptr ? myEnemy : otherEnemy);
		if (myEnemy != nullptr) damageToThis += synergyBonus;
		else damageToOther += synergyBonus;
	}
			const bool myEnemyWasFullHp =
				myEnemy != nullptr &&
				otherPlayer != nullptr &&
				myEnemy->GetHP() == myEnemy->GetMaxHP();
			const bool otherEnemyWasFullHp =
				otherEnemy != nullptr &&
				myPlayer != nullptr &&
				otherEnemy->GetHP() == otherEnemy->GetMaxHP();
			const int myEnemyHpBefore =
				myEnemy != nullptr ? myEnemy->GetHP() : 0;
			const int otherEnemyHpBefore =
				otherEnemy != nullptr ? otherEnemy->GetHP() : 0;
			const bool myEnemyWasDefeated =
				myEnemy != nullptr && myEnemy->IsDefeated();
			const bool otherEnemyWasDefeated =
				otherEnemy != nullptr && otherEnemy->IsDefeated();
			const Vector3 myEnemyFeedbackPosition =
				myEnemy != nullptr ? myEnemy->GetPosition() : Vector3::Zero;
			const Vector3 otherEnemyFeedbackPosition =
				otherEnemy != nullptr ? otherEnemy->GetPosition() : Vector3::Zero;

			if (isPlayerEnemyCollision)
			{
				BalanceLogger::GetInstance().RecordDamageCollision(
					BalanceCollisionType::PlayerEnemy);
			}
			else if (isEnemyEnemyCollision)
			{
				// 両方の敵にダメージが入っても、衝突回数は1回。
				BalanceLogger::GetInstance().RecordDamageCollision(
					BalanceCollisionType::EnemyEnemy);
			}

			if (myEnemy != nullptr)
			{
				myEnemy->TakeDamage(myEnemy->AdjustCollisionDamage(damageToThis, other->GetPosition()));
				const int appliedDamage = (std::max)(
					0,
					myEnemyHpBefore - myEnemy->GetHP());
				if (!myEnemyWasDefeated &&
					(appliedDamage > 0 || myEnemy->IsDefeated()))
				{
					GameBattleEvents::CombatFeedback(
						myEnemyFeedbackPosition,
						appliedDamage,
						myEnemy->IsDefeated(),
						isEnemyEnemyCollision);
				}
				if (myEnemyWasFullHp &&
					!myEnemy->IsDefeated())
				{
					GameBattleEvents::BalanceAutoFullHpEnemySurvived();
				}
				if (isPlayerEnemyCollision || isEnemyEnemyCollision)
				{
					myEnemy->RegisterCollisionForStage(
						reinterpret_cast<std::uintptr_t>(other));
				}
			}

			if (otherEnemy != nullptr)
			{
				otherEnemy->TakeDamage(otherEnemy->AdjustCollisionDamage(damageToOther, GetPosition()));
				const int appliedDamage = (std::max)(
					0,
					otherEnemyHpBefore - otherEnemy->GetHP());
				if (!otherEnemyWasDefeated &&
					(appliedDamage > 0 || otherEnemy->IsDefeated()))
				{
					GameBattleEvents::CombatFeedback(
						otherEnemyFeedbackPosition,
						appliedDamage,
						otherEnemy->IsDefeated(),
						isEnemyEnemyCollision);
				}
				if (otherEnemyWasFullHp &&
					!otherEnemy->IsDefeated())
				{
					GameBattleEvents::BalanceAutoFullHpEnemySurvived();
				}
				if (isPlayerEnemyCollision || isEnemyEnemyCollision)
				{
					otherEnemy->RegisterCollisionForStage(
						reinterpret_cast<std::uintptr_t>(m_BallComponent));
				}
			}

			if (myEnemy != nullptr && otherPlayer != nullptr && !myEnemyWasDefeated)
			{
				GameBattleEvents::PlayerChainImpact(
					myEnemy,
					damageToThis,
					otherPlayer->GetStatus().chainImpactRadius);
			}
			else if (otherEnemy != nullptr && myPlayer != nullptr && !otherEnemyWasDefeated)
			{
				GameBattleEvents::PlayerChainImpact(
					otherEnemy,
					damageToOther,
					myPlayer->GetStatus().chainImpactRadius);
			}

    if (isPlayerEnemyCollision)
    {
        first = CapturePhysicsBody();
        second = otherContact.CapturePhysicsBody();
        if (BallPhysicsRules::StopAnchor(first, m_PhysicsComponent->Acceleration()))
			GameBattleEvents::AnchorStopped(otherEnemy);
        if (BallPhysicsRules::StopAnchor(second, otherCollision->m_PhysicsComponent->Acceleration()))
			GameBattleEvents::AnchorStopped(myEnemy);
        CommitPhysicsBody(first);
        otherContact.CommitPhysicsBody(second);
    }

			if (isPlayerEnemyCollision)
			{
				GameBattleEvents::DamageBallCollision(
						DamageBallCollisionType::PlayerEnemy);
			}
			else if (isEnemyEnemyCollision)
			{
				GameBattleEvents::DamageBallCollision(
						DamageBallCollisionType::EnemyEnemy);
			}
}

void BallCollisionComponent::ResetShotAbilityState()
{
	m_PierceUseCount = 0;
	m_PiercedBalls.clear();
    m_TouchingBalls.clear();
}

DirectX::SimpleMath::Vector3 BallCollisionComponent::GetPosition() const
{
	return m_BallComponent->GetPosition();
}

void BallCollisionComponent::SetPosition(
	const DirectX::SimpleMath::Vector3& position)
{
	m_BallComponent->SetPosition(position);
}

void BallCollisionComponent::Translate(
	const DirectX::SimpleMath::Vector3& movement)
{
	m_BallComponent->Translate(movement);
}

Collision::Sphere BallCollisionComponent::GetSphere() const
{
	return m_BallComponent->GetSphere();
}

int BallCollisionComponent::GetAttack() const
{
	return m_BallComponent->GetAttack();
}

bool BallCollisionComponent::HasPierceAbility() const
{
	return m_BallComponent->HasPierceAbility();
}

bool BallCollisionComponent::CheckPocketHitAlongMovement(
	const Vector3& movementStart,
	const std::vector<Pocket*>& pockets)
{
    if (const auto* enemy = GetGameObject()->GetComponent<EnemyBall>(); enemy && enemy->IsArmorBoss()) return false;
	if (m_BallComponent == nullptr)
	{
		return false;
	}
	// 通常ダメージで倒れた敵はショット停止まで物理判定が残り、
	// その間にポケットへ落ちる可能性がある。敗北したプレイヤーにはポケット処理を行わない。
	if (m_BallComponent->IsDefeated() &&
		GetGameObject()->GetComponent<EnemyBall>() == nullptr)
	{
		return false;
	}

    for (Pocket* pocket : pockets)
    {
        if (pocket == nullptr || pocket->GetGameObject() == nullptr ||
            !pocket->GetGameObject()->IsActive()) continue;
        if (BallPhysicsRules::PocketHit(movementStart, GetPosition(), GetSphere().radius, pocket->GetSphere()))
        {
            OnPocketHit();
            return true;
        }
    }
	return false;
}

void BallCollisionComponent::OnPocketHit()
{
	m_BallComponent->OnPocketHit();
}

BallPhysicsRules::Body BallCollisionComponent::CapturePhysicsBody() const
{
    BallPhysicsRules::Body body;
    body.id = reinterpret_cast<std::uintptr_t>(m_BallComponent);
    body.position = GetPosition();
    body.velocity = m_PhysicsComponent->Velocity();
    body.status = m_BallComponent->GetStatus();
    body.status.radius = m_PhysicsComponent->Radius();
    body.status.mass = m_PhysicsComponent->Mass();
    body.status.restitution = m_PhysicsComponent->Restitution();
    body.status.friction = m_PhysicsComponent->Friction();
    body.player = GetGameObject()->GetComponent<PlayerBall>() != nullptr;
    const auto* enemy = GetGameObject()->GetComponent<EnemyBall>();
    body.enemy = enemy != nullptr;
    body.boss = enemy && enemy->IsArmorBoss();
	if (enemy != nullptr)
		body.nextKnockbackMultiplier = enemy->GetNextKnockbackMultiplier();
    body.breakBall = GetGameObject()->GetComponent<BreakBall>() != nullptr;
    body.pierceUses = m_PierceUseCount;
    body.pierceLimit = GameBattleView::PierceMaximumUses();
    body.pierceRetention = GameBattleView::PierceSpeedRetention();
    for (auto* contact : m_PiercedBalls) body.pierced.push_back(reinterpret_cast<std::uintptr_t>(contact));
    for (auto* contact : m_TouchingBalls) body.touching.push_back(reinterpret_cast<std::uintptr_t>(contact));
    return body;
}

void BallCollisionComponent::CommitPhysicsBody(const BallPhysicsRules::Body& body)
{
    SetPosition(body.position);
    m_PhysicsComponent->Velocity() = body.velocity;
    m_PierceUseCount = body.pierceUses;
	if (body.consumedKnockbackMultiplier)
	{
		if (auto* enemy = GetGameObject()->GetComponent<EnemyBall>())
			enemy->ConsumeLightweight();
	}
    m_PiercedBalls.clear();
    m_TouchingBalls.clear();
    for (auto id : body.touching) m_TouchingBalls.push_back(reinterpret_cast<const BallComponent*>(id));
    for (auto id : body.pierced) m_PiercedBalls.push_back(reinterpret_cast<const BallComponent*>(id));
}
