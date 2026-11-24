#include "BallCollisionComponent.h"

#include "BalanceLogger.h"
#include "BallComponent.h"
#include "BallMechanics.h"
#include "BallPhysicsRules.h"
#include "BallPhysicsComponent.h"
#include "EnemyBall.h"
#include "BreakBall.h"
#include "PierceMinion.h"
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
    /// <summary>
    /// 攻撃元のボールやショットの種別に応じてダメージ種別を決める。
    /// </summary>
    EnemyDamageKind DamageKindFromSource(
        const BallComponent* source,
        const PlayerBall* sourcePlayer)
    {
        if (sourcePlayer != nullptr &&
            GameBattleView::IsCurrentShotPierceCategory())
        {
            return EnemyDamageKind::PierceAxis;
        }
        const GameObject* owner =
            source != nullptr ? source->GetGameObject() : nullptr;
        if (owner != nullptr &&
            owner->GetComponent<PierceMinion>() != nullptr)
        {
            return EnemyDamageKind::PierceAxis;
        }
        return EnemyDamageKind::Other;
    }

	struct PierceTelemetrySource
	{
		PierceAttackSource type = PierceAttackSource::Other;
		std::uintptr_t key = 0;
	};

	/// <summary>
	/// 貫通成立の有無と攻撃元から、接触に対応する貫通攻撃の発生源を決める。
	/// </summary>
	PierceTelemetrySource PierceSourceFromContact(
		const BallComponent* source,
		const PlayerBall* sourcePlayer,
		bool consumedPierceUse)
	{
		if (sourcePlayer != nullptr &&
			GameBattleView::IsCurrentShotPierceCategory())
		{
			return {
				consumedPierceUse
					? PierceAttackSource::PlayerPierce
					: PierceAttackSource::PlayerDirect,
				0,
			};
		}
		const GameObject* owner =
			source != nullptr ? source->GetGameObject() : nullptr;
		PierceMinion* minion =
			owner != nullptr ? owner->GetComponent<PierceMinion>() : nullptr;
		if (minion != nullptr)
		{
			return {
				PierceAttackSource::Minion,
				reinterpret_cast<std::uintptr_t>(minion),
			};
		}
		return {};
	}

    struct LiveShotRules final
    {
        /// <summary>
        /// 壁反射によるダメージ倍率を取得し、その適用分を消費する。
        /// </summary>
        int ConsumeBankShotDamageMultiplier()
        {
            return GameBattleCommands::ConsumeBankShotDamageMultiplier();
        }

        /// <summary>
        /// プレイヤーから敵へのレリック追加ダメージを取得して消費する。
        /// </summary>
        int ConsumePlayerEnemyRelicDamageBonus()
        {
            return GameBattleCommands::ConsumePlayerEnemyRelicDamageBonus();
        }

        /// <summary>
        /// 現在のショットに適用される衝突攻撃力の加算値を返す。
        /// </summary>
        int GetCurrentShotCollisionAttackBonus() const
        {
            return GameBattleView::CurrentShotCollisionAttackBonus();
        }
    };
}

/// <summary>
/// 所有オブジェクトの物理コンポーネントを取得し、必要な構成が欠けていれば破棄を要求する。
/// </summary>
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

/// <summary>
/// 衝突処理の対象となるボールコンポーネントを登録する。
/// </summary>
void BallCollisionComponent::BindBall(BallComponent& ball)
{
	m_BallComponent = &ball;
}

/// <summary>
/// 所有オブジェクトと関連コンポーネントの状態から衝突計算の対象か確認する。
/// </summary>
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

/// <summary>
/// 活動中のボール一覧を参照し、消えた相手を貫通と接触の記録から除く。
/// </summary>
void BallCollisionComponent::ForgetMissingContacts(const std::vector<BallComponent*>& activeBalls)
{
    const auto missing = [&activeBalls](const BallComponent* other) {
        return std::find(activeBalls.begin(), activeBalls.end(), other) == activeBalls.end();
    };
    std::erase_if(m_PiercedBalls, missing);
    std::erase_if(m_TouchingBalls, missing);
}

/// <summary>
/// 移動中のポケット侵入と壁への衝突を処理し、速度や関連する効果を反映する。
/// </summary>
void BallCollisionComponent::ResolveEnvironment(
	const Vector3& movementStart,
	const std::vector<Collision::Segment>& walls,
	const Vector3& interiorReference,
	const std::vector<Pocket*>& pockets)
{
	if (!CanSimulate() || CheckPocketHitAlongMovement(movementStart, pockets)) return;
    auto body = CapturePhysicsBody();
	auto* enemy = GetGameObject()->GetComponent<EnemyBall>();
	const bool telemetryActive =
		BalanceLogger::GetInstance().IsShotActive();
	const auto resolveWall = [&](
		const Collision::Segment& wall,
		const Vector3& interior,
		int wallIndex,
		const char* wallGroup)
	{
		const Vector3 velocityBefore = body.velocity;
		const bool bounced = BallPhysicsRules::Wall(body, wall, interior);
		const Vector3 velocityAfterPhysics = body.velocity;
		if (bounced)
		{
			GameBattleEvents::WallImpact(
				body.position,
				velocityBefore.Length(),
				GetGameObject());
		}
		if (bounced && body.player)
			GameBattleEvents::PlayerWallCollision(0, body.position, body.velocity);
		const bool wasPinballTarget = bounced && telemetryActive &&
			enemy != nullptr &&
			GameBattleView::IsPinballTarget(
				reinterpret_cast<std::uintptr_t>(enemy));
		if (bounced && enemy != nullptr)
		{
			enemy->RegisterWallImpact(velocityBefore.Length());
			GameBattleEvents::EnemyWallCollision(
				enemy, velocityBefore, body.velocity);
		}
		if (bounced && telemetryActive && (body.player || wasPinballTarget))
		{
			BalanceLogger::GetInstance().RecordWallReflection(
				wallIndex,
				wallGroup,
				body.player ? "player" : "pinball_target",
				body.position.x,
				body.position.z,
				velocityBefore.x,
				velocityBefore.z,
				velocityAfterPhysics.x,
				velocityAfterPhysics.z,
				body.velocity.x,
				body.velocity.z);
		}
	};
    if (body.boss)
    {
		const auto& bossWalls = BallPhysicsRules::BossWalls();
		for (std::size_t index = 0; index < bossWalls.size(); ++index)
			resolveWall(
				bossWalls[index],
				Vector3::Zero,
				static_cast<int>(index),
				"boss");
    }
	for (std::size_t index = 0; index < walls.size(); ++index)
    {
		resolveWall(
			walls[index],
			interiorReference,
			static_cast<int>(index),
			"table");
    }
    // Walls only change motion; rebuilding unchanged contact histories for each
    // wall copied the same vectors repeatedly during every TOI iteration.
    SetPosition(body.position);
    m_PhysicsComponent->Velocity() = body.velocity;
	if (CanSimulate()) CheckPocketHitAlongMovement(movementStart, pockets);
}

/// <summary>
/// 貫通中の敵を調べ、めり込みから抜ける方向を求める。
/// </summary>
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

/// <summary>
/// 2球の衝突応答と押し戻しを適用し、能力やダメージ、撃破に伴う効果を処理する。
/// </summary>
void BallCollisionComponent::ResolveBallPair(BallCollisionComponent& otherContact)
{
	if (&otherContact == this || !CanSimulate() || !otherContact.CanSimulate()) return;
	BallCollisionComponent* otherCollision = &otherContact;
	BallComponent* other = otherCollision->m_BallComponent;
    auto first = CapturePhysicsBody();
    auto second = otherContact.CapturePhysicsBody();
	const Vector3 firstPositionBefore = first.position;
	const Vector3 secondPositionBefore = second.position;
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
    // Living escorts and their in-shot wrecks use the same one-use impact rule.
    if (myEnemy && otherEnemy)
    {
        if (myEnemy->IsBossMinion() && otherEnemy->IsArmorBoss() && !otherEnemy->IsDefeated())
        { myEnemy->ConsumeBossMinion(*otherEnemy); return; }
        if (otherEnemy->IsBossMinion() && myEnemy->IsArmorBoss() && !myEnemy->IsDefeated())
        { otherEnemy->ConsumeBossMinion(*myEnemy); return; }
    }
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
	Vector3 impactNormal = firstPositionBefore - secondPositionBefore;
	impactNormal.y = 0.0f;
	if (impactNormal.LengthSquared() > 0.00000001f)
	{
		impactNormal.Normalize();
	}
	else
	{
		impactNormal = secondVelocityBefore - firstVelocityBefore;
		impactNormal.y = 0.0f;
		if (impactNormal.LengthSquared() > 0.00000001f)
			impactNormal.Normalize();
		else
			impactNormal = Vector3::UnitX;
	}
	float relativeApproachSpeed = (std::max)(
		0.0f,
		-(firstVelocityBefore - secondVelocityBefore).Dot(impactNormal));
	if (!std::isfinite(relativeApproachSpeed))
	{
		relativeApproachSpeed = 0.0f;
	}
	const Vector3 contactPosition =
		(firstPositionBefore + secondPositionBefore) * 0.5f;
	int impactDamage = 0;
	bool impactDefeated = false;
	int pinballCollisionBonus = 0;
	if (isEnemyEnemyCollision &&
		myEnemy->CanParticipateInCombat() &&
		otherEnemy->CanParticipateInCombat())
	{
		pinballCollisionBonus = GameBattleEvents::EnemyEnemySynergyCollision(
			myEnemy, otherEnemy, firstVelocityBefore, secondVelocityBefore);
	}
	if (isPlayerEnemyCollision)
	{
		EnemyBall* target = myEnemy != nullptr ? myEnemy : otherEnemy;
		const bool playerPierced =
			(myPlayer != nullptr && first.pierceUses > firstPierceUsesBefore) ||
			(otherPlayer != nullptr && second.pierceUses > secondPierceUsesBefore);
		if (playerPierced && target != nullptr &&
			target->CanParticipateInCombat())
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
        myEnemy && myEnemy->IsDefeated(), otherEnemy && otherEnemy->IsDefeated(), liveShotRules,
        GameBattleView::CurrentShotImpactAttack());
	int damageToThis = damage.first, damageToOther = damage.second;
	if (isEnemyEnemyCollision)
	{
		damageToThis = GameBattleView::ScaleEnemyCollisionDamage(damageToThis);
		damageToOther = GameBattleView::ScaleEnemyCollisionDamage(damageToOther);
		damageToThis += pinballCollisionBonus;
		damageToOther += pinballCollisionBonus;
	}
	if (isPlayerEnemyCollision)
	{
		EnemyBall* target = myEnemy != nullptr ? myEnemy : otherEnemy;
		if (target == nullptr || !target->CanParticipateInCombat())
		{
			target = nullptr;
		}
		const Vector3& enemyVelocityBefore = myEnemy != nullptr
			? firstVelocityBefore
			: secondVelocityBefore;
		const PlayerEnemySynergyResult synergy = target != nullptr
			? GameBattleEvents::PlayerEnemySynergyCollision(
				target,
				enemyVelocityBefore)
			: PlayerEnemySynergyResult{};
		if (myEnemy != nullptr) damageToThis += synergy.damageBonus;
		else damageToOther += synergy.damageBonus;
		if (synergy.stopPlayer)
		{
			PlayerBall* player = myPlayer != nullptr ? myPlayer : otherPlayer;
			if (player != nullptr && player->GetBall() != nullptr)
			{
				player->GetBall()->GetMutableVelocity() = Vector3::Zero;
				player->GetBall()->GetMutableAcceleration() = Vector3::Zero;
			}
		}
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
				BalanceLogger::GetInstance().RecordEnemyEnemyCollision(
					reinterpret_cast<std::uintptr_t>(myEnemy),
					myEnemy->GetEnemyId(),
					reinterpret_cast<std::uintptr_t>(otherEnemy),
					otherEnemy->GetEnemyId(),
					relativeApproachSpeed,
					contactPosition.x,
					contactPosition.z);
			}

			if (myEnemy != nullptr)
			{
				const int adjustedDamage =
					myEnemy->AdjustCollisionDamage(
						damageToThis,
						other->GetPosition());
				const int calculatedDamage =
					myEnemy->CalculateDamageTaken(adjustedDamage);
				const int baseAdjustedDamage = pinballCollisionBonus > 0
					? myEnemy->AdjustCollisionDamage(
						(std::max)(0, damageToThis - pinballCollisionBonus),
						other->GetPosition())
					: 0;
				const int baseCalculatedDamage = pinballCollisionBonus > 0
					? myEnemy->CalculateDamageTaken(baseAdjustedDamage)
					: 0;
				const PierceTelemetrySource source =
					PierceSourceFromContact(
						other,
						otherPlayer,
						otherPlayer != nullptr &&
						second.pierceUses > secondPierceUsesBefore);
				myEnemy->TakeDamage(
					adjustedDamage,
					DamageKindFromSource(other, otherPlayer));
				const int appliedDamage = (std::max)(
					0,
					myEnemyHpBefore - myEnemy->GetHP());
				impactDamage += appliedDamage;
				if (source.type != PierceAttackSource::Other ||
					GameBattleView::IsCurrentShotBounceCategory())
				{
					BalanceLogger::GetInstance().RecordPierceAttack(
						source.type,
						source.key,
						reinterpret_cast<std::uintptr_t>(myEnemy),
						calculatedDamage,
						appliedDamage,
						!myEnemyWasDefeated);
				}
				if (pinballCollisionBonus > 0)
				{
					const int calculatedBonus = (std::max)(
						0,
						calculatedDamage - baseCalculatedDamage);
					const int baseAppliedDamage = myEnemyWasDefeated
						? 0
						: (std::min)(myEnemyHpBefore, baseCalculatedDamage);
					BalanceLogger::GetInstance().RecordBounceSpecialDamage(
						pinballCollisionBonus,
						calculatedBonus,
						(std::max)(0, appliedDamage - baseAppliedDamage));
				}
				impactDefeated = impactDefeated ||
					(!myEnemyWasDefeated && myEnemy->IsDefeated());
				if (!myEnemyWasDefeated &&
					(appliedDamage > 0 || myEnemy->IsDefeated()))
				{
					GameBattleEvents::CombatFeedback(
						myEnemyFeedbackPosition,
						appliedDamage,
						myEnemy->IsDefeated(),
						isEnemyEnemyCollision,
						myEnemy);
				}
				if (myEnemyWasFullHp &&
					!myEnemy->IsDefeated())
				{
					GameBattleEvents::BalanceAutoFullHpEnemySurvived();
				}
				const bool countsForMyEnemyCombatState =
					(isPlayerEnemyCollision && !myEnemyWasDefeated) ||
					(isEnemyEnemyCollision &&
					 !myEnemyWasDefeated && !otherEnemyWasDefeated);
				if (countsForMyEnemyCombatState)
				{
					myEnemy->RegisterCollisionForStage(
						reinterpret_cast<std::uintptr_t>(other));
				}
			}

			if (otherEnemy != nullptr)
			{
				const int adjustedDamage =
					otherEnemy->AdjustCollisionDamage(
						damageToOther,
						GetPosition());
				const int calculatedDamage =
					otherEnemy->CalculateDamageTaken(adjustedDamage);
				const int baseAdjustedDamage = pinballCollisionBonus > 0
					? otherEnemy->AdjustCollisionDamage(
						(std::max)(0, damageToOther - pinballCollisionBonus),
						GetPosition())
					: 0;
				const int baseCalculatedDamage = pinballCollisionBonus > 0
					? otherEnemy->CalculateDamageTaken(baseAdjustedDamage)
					: 0;
				const PierceTelemetrySource source =
					PierceSourceFromContact(
						m_BallComponent,
						myPlayer,
						myPlayer != nullptr &&
						first.pierceUses > firstPierceUsesBefore);
				otherEnemy->TakeDamage(
					adjustedDamage,
					DamageKindFromSource(m_BallComponent, myPlayer));
				const int appliedDamage = (std::max)(
					0,
					otherEnemyHpBefore - otherEnemy->GetHP());
				impactDamage += appliedDamage;
				if (source.type != PierceAttackSource::Other ||
					GameBattleView::IsCurrentShotBounceCategory())
				{
					BalanceLogger::GetInstance().RecordPierceAttack(
						source.type,
						source.key,
						reinterpret_cast<std::uintptr_t>(otherEnemy),
						calculatedDamage,
						appliedDamage,
						!otherEnemyWasDefeated);
				}
				if (pinballCollisionBonus > 0)
				{
					const int calculatedBonus = (std::max)(
						0,
						calculatedDamage - baseCalculatedDamage);
					const int baseAppliedDamage = otherEnemyWasDefeated
						? 0
						: (std::min)(otherEnemyHpBefore, baseCalculatedDamage);
					BalanceLogger::GetInstance().RecordBounceSpecialDamage(
						pinballCollisionBonus,
						calculatedBonus,
						(std::max)(0, appliedDamage - baseAppliedDamage));
				}
				impactDefeated = impactDefeated ||
					(!otherEnemyWasDefeated && otherEnemy->IsDefeated());
				if (!otherEnemyWasDefeated &&
					(appliedDamage > 0 || otherEnemy->IsDefeated()))
				{
					GameBattleEvents::CombatFeedback(
						otherEnemyFeedbackPosition,
						appliedDamage,
						otherEnemy->IsDefeated(),
						isEnemyEnemyCollision,
						otherEnemy);
				}
				if (otherEnemyWasFullHp &&
					!otherEnemy->IsDefeated())
				{
					GameBattleEvents::BalanceAutoFullHpEnemySurvived();
				}
				const bool countsForOtherEnemyCombatState =
					(isPlayerEnemyCollision && !otherEnemyWasDefeated) ||
					(isEnemyEnemyCollision &&
					 !myEnemyWasDefeated && !otherEnemyWasDefeated);
				if (countsForOtherEnemyCombatState)
				{
					otherEnemy->RegisterCollisionForStage(
						reinterpret_cast<std::uintptr_t>(m_BallComponent));
				}
			}

			if (myEnemy != nullptr && otherPlayer != nullptr && !myEnemyWasDefeated)
			{
				GameBattleEvents::PlayerChainImpact(
					myEnemyFeedbackPosition,
					myEnemy,
					damageToThis,
					otherPlayer->GetStatus().chainImpactRadius);
			}
			else if (otherEnemy != nullptr && myPlayer != nullptr && !otherEnemyWasDefeated)
			{
				GameBattleEvents::PlayerChainImpact(
					otherEnemyFeedbackPosition,
					otherEnemy,
					damageToOther,
					myPlayer->GetStatus().chainImpactRadius);
			}

	if (isPlayerEnemyCollision || isEnemyEnemyCollision)
	{
		EnemyBall* firstTarget = myEnemy != nullptr ? myEnemy : otherEnemy;
		EnemyBall* secondTarget = isEnemyEnemyCollision ? otherEnemy : nullptr;
		GameBattleEvents::CollisionImpact(
			contactPosition,
			firstTarget != nullptr ? firstTarget->GetPosition() : contactPosition,
			secondTarget != nullptr ? secondTarget->GetPosition() : contactPosition,
			relativeApproachSpeed,
			isEnemyEnemyCollision
				? CombatCollisionKind::EnemyEnemy
				: CombatCollisionKind::PlayerEnemy,
			impactDamage,
			impactDefeated,
			firstTarget,
			secondTarget);
	}

    if (isPlayerEnemyCollision)
    {
        first = CapturePhysicsBody();
        second = otherContact.CapturePhysicsBody();
        if (BallPhysicsRules::StopAnchor(first, m_PhysicsComponent->Acceleration()))
		{
			if (otherEnemy != nullptr && !otherEnemyWasDefeated)
			{
				GameBattleEvents::AnchorStopped(
					otherEnemy->CanParticipateInCombat() ? otherEnemy : nullptr);
			}
		}
        if (BallPhysicsRules::StopAnchor(second, otherCollision->m_PhysicsComponent->Acceleration()))
		{
			if (myEnemy != nullptr && !myEnemyWasDefeated)
			{
				GameBattleEvents::AnchorStopped(
					myEnemy->CanParticipateInCombat() ? myEnemy : nullptr);
			}
		}
        CommitPhysicsBody(first);
        otherContact.CommitPhysicsBody(second);
    }

			const bool playerHitCombatEnemy =
				isPlayerEnemyCollision &&
				((myEnemy != nullptr && !myEnemyWasDefeated) ||
				 (otherEnemy != nullptr && !otherEnemyWasDefeated));
			const bool combatEnemyPair =
				isEnemyEnemyCollision &&
				!myEnemyWasDefeated && !otherEnemyWasDefeated;
			if (playerHitCombatEnemy)
			{
				GameBattleEvents::DamageBallCollision(
						DamageBallCollisionType::PlayerEnemy);
			}
			else if (combatEnemyPair)
			{
				GameBattleEvents::DamageBallCollision(
						DamageBallCollisionType::EnemyEnemy);
			}
}

/// <summary>
/// 貫通使用回数と、ショット内の貫通・接触記録を初期化する。
/// </summary>
void BallCollisionComponent::ResetShotAbilityState()
{
	m_PierceUseCount = 0;
	m_PiercedBalls.clear();
    m_TouchingBalls.clear();
}

/// <summary>
/// 紐づけられたボールの現在の位置を返す。
/// </summary>
DirectX::SimpleMath::Vector3 BallCollisionComponent::GetPosition() const
{
	return m_BallComponent->GetPosition();
}

/// <summary>
/// 紐づけられたボールの位置を指定座標に更新する。
/// </summary>
void BallCollisionComponent::SetPosition(
	const DirectX::SimpleMath::Vector3& position)
{
	m_BallComponent->SetPosition(position);
}

/// <summary>
/// 紐づけられたボールを指定した移動量だけ移動させる。
/// </summary>
void BallCollisionComponent::Translate(
	const DirectX::SimpleMath::Vector3& movement)
{
	m_BallComponent->Translate(movement);
}

/// <summary>
/// 紐づけられたボールの中心と半径を衝突判定用の球として返す。
/// </summary>
Collision::Sphere BallCollisionComponent::GetSphere() const
{
	return m_BallComponent->GetSphere();
}

/// <summary>
/// 戦闘に参加できるボールの攻撃力を返し、参加できない敵の攻撃力をゼロにする。
/// </summary>
int BallCollisionComponent::GetAttack() const
{
	if (const EnemyBall* enemy =
		GetGameObject()->GetComponent<EnemyBall>();
		enemy != nullptr && !enemy->CanParticipateInCombat())
	{
		return 0;
	}
	return m_BallComponent->GetAttack();
}

/// <summary>
/// 紐づけられたボールが貫通能力を持つかを判定する。
/// </summary>
bool BallCollisionComponent::HasPierceAbility() const
{
	return m_BallComponent->HasPierceAbility();
}

/// <summary>
/// 移動区間と有効なポケットの接触を調べ、最初の接触を処理する。
/// </summary>
bool BallCollisionComponent::CheckPocketHitAlongMovement(
	const Vector3& movementStart,
	const std::vector<Pocket*>& pockets)
{
	if (m_BallComponent == nullptr)
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

/// <summary>
/// ポケットへの接触をボール側の処理へ引き渡す。
/// </summary>
void BallCollisionComponent::OnPocketHit()
{
	m_BallComponent->OnPocketHit();
}

/// <summary>
/// 位置・速度・ステータス・能力の状態を衝突計算用の構造体にまとめる。
/// </summary>
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
	body.minion = GetGameObject()->GetComponent<PierceMinion>() != nullptr;
    const auto* enemy = GetGameObject()->GetComponent<EnemyBall>();
    // A defeated enemy remains an active physical sphere, but it is no longer
    // an enemy interaction target. Keeping this flag set made piercing balls
    // enter the overlap-bypass/forced-exit path against retained corpses and
    // could keep their speed above the shot-stop threshold indefinitely.
    body.enemy = enemy != nullptr && enemy->CanParticipateInCombat();
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

/// <summary>
/// 衝突計算後の位置、速度、貫通回数などをボールの実体へ反映する。
/// </summary>
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
