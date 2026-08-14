#include "BallCollisionComponent.h"

#include "BalanceLogger.h"
#include "BallComponent.h"
#include "BallPhysicsComponent.h"
#include "EnemyBall.h"
#include "Game.h"
#include "GameObject.h"
#include "PlayerBall.h"
#include "Pocket.h"
#include "TableFrame.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace DirectX::SimpleMath;

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

void BallCollisionComponent::ResolveMovementAndCollisions(
	BallComponent& ball)
{
	m_BallComponent = &ball;
	if (GetGameObject() == nullptr || !GetGameObject()->IsActive())
	{
		return;
	}

	// A ball can already overlap a pocket after another ball resolves its
	// collision first. Handle that state before applying this frame's motion.
	if (CheckPocketHitAlongMovement(GetPosition()))
	{
		return;
	}

	std::vector<Collision::Segment> walls;                          // テーブル枠から集めた壁情報
	std::vector<TableFrame*> frames = Game::GetInstance()->GetComponents<TableFrame>();

	for (TableFrame* frame : frames)
	{
		std::vector<Collision::Segment> frameWalls = frame->GetWalls();

		walls.insert(
			walls.end(),
			frameWalls.begin(),
			frameWalls.end());
	}

	if (!walls.empty())
	{
		Vector3 interiorReference = Vector3::Zero;
		for (const Collision::Segment& wall : walls)
		{
			interiorReference += wall.start + wall.end;
		}
		interiorReference /= static_cast<float>(walls.size() * 2u);

		// 1フレームの移動距離を計算
		float moveDistance = m_PhysicsComponent->Velocity().Length();

		// 1ステップで進んでいい最大の距離（すり抜けないよう半径の半分以下にする）
		float maxStep = m_PhysicsComponent->Radius() * 0.5f;

		// 必要な分割数（速度が遅ければ1回、速ければ自動で増える）
		int subSteps = (std::max)(1, static_cast<int>(std::ceil(moveDistance / maxStep)));

		std::vector<BallComponent*> balls = Game::GetInstance()->GetComponents<BallComponent>();
		for (BallComponent* other : balls)
		{
			if (other == m_BallComponent) continue;
			if (other == nullptr || other->GetGameObject() == nullptr ||
				!other->GetGameObject()->IsActive()) continue;

			BallCollisionComponent* otherCollision =
				other->GetGameObject()->GetComponent<BallCollisionComponent>();
			if (otherCollision == nullptr) continue;

			Vector3 relativeVelocity = m_PhysicsComponent->Velocity() - otherCollision->m_PhysicsComponent->Velocity();
			float relativeSpeed = relativeVelocity.Length();

			int relativeSubSteps = (std::max)(1, static_cast<int>(std::ceil(relativeSpeed / maxStep)));
			subSteps = (std::max)(subSteps, relativeSubSteps);
		}

		// 1ステップあたりの移動量
		Vector3 stepVelocity = m_PhysicsComponent->Velocity() / static_cast<float>(subSteps);

		for (int step = 0; step < subSteps; step++)
		{
			const Vector3 movementStart = GetPosition();

			// 少しだけ移動させる
			Translate(stepVelocity);
			// Check the swept path, not only the final frame position. This
			// prevents fast balls from passing completely through a pocket.
			if (CheckPocketHitAlongMovement(movementStart))
			{
				return;
			}

			// その位置で壁との当たり判定
			for (int i = 0; i < static_cast<int>(walls.size()); i++)
			{
				Vector3 contactPoint;
				const Vector3 position = GetPosition();
				float distance = Collision::DistancePointToSegment(position, walls[i], contactPoint);

				// 距離が半径以下なら衝突
				if (distance <= m_PhysicsComponent->Radius())
				{
					// 法線は速度ではなく、常にテーブル内側へ向ける。
					// 反射直後の内向き移動を「外側への衝突」と誤判定しないため。
					Vector3 wallDirection = walls[i].end - walls[i].start;
					wallDirection.y = 0.0f;
					if (wallDirection.LengthSquared() <= 0.0001f)
					{
						continue;
					}
					wallDirection.Normalize();
					Vector3 normal(
						-wallDirection.z,
						0.0f,
						wallDirection.x);
					if (Collision::Dot(
						interiorReference - contactPoint,
						normal) < 0.0f)
					{
						normal = -normal;
					}

					// 1. めり込み防止（衝突点から半径分押し返す）
					SetPosition(contactPoint + normal * m_PhysicsComponent->Radius());

					// 2. 反射処理
					float dot = Collision::Dot(m_PhysicsComponent->Velocity(), normal);
					if (dot < 0)
					{
						if (GetGameObject()->GetComponent<PlayerBall>() != nullptr)
						{
							Game::GetInstance()->NotifyPlayerWallCollision();
						}

						float restitution = m_PhysicsComponent->Restitution(); // 反発係数
						const Vector3 reflectedVelocity =
							m_PhysicsComponent->Velocity() -
							normal * (2.0f * dot);
						m_PhysicsComponent->Velocity() =
							reflectedVelocity * restitution;

						// 反射したので、残りのステップの移動方向も「反射後の速度」に更新する
						stepVelocity = m_PhysicsComponent->Velocity() / static_cast<float>(subSteps);
					}
				}
			}

			// ボール同士の衝突判定（二重処理防止版）
			std::vector<BallComponent*> balls = Game::GetInstance()->GetComponents<BallComponent>();
			bool foundSelf = false;  // 自分を見つけたかのフラグ

			for (BallComponent* other : balls)
			{
				// 自分を見つけるまでスキップ
				if (!foundSelf)
				{
					if (other == m_BallComponent) foundSelf = true;
					continue;  // 自分自身も含めてスキップ
				}

				if (other == nullptr || other->GetGameObject() == nullptr ||
					!other->GetGameObject()->IsActive())
				{
					continue;
				}

				// ↓ ここから「自分より後ろのボール」とだけ判定される
				BallCollisionComponent* otherCollision =
					other->GetGameObject()->GetComponent<BallCollisionComponent>();
				if (otherCollision == nullptr) continue;

				Vector3 diff = GetPosition() - other->GetPosition();
				float distance = diff.Length();
				float minDist = m_PhysicsComponent->Radius() + otherCollision->m_PhysicsComponent->Radius();

				if (distance >= minDist)
				{
					if (m_PiercedBall == other)
					{
						m_PiercedBall = nullptr;
					}
					if (otherCollision->m_PiercedBall == m_BallComponent)
					{
						otherCollision->m_PiercedBall = nullptr;
					}
				}

				const bool ignoresPiercedContact =
					m_PiercedBall == other ||
					otherCollision->m_PiercedBall == m_BallComponent;

				if (distance < minDist &&
					distance > 0.0001f &&
					!ignoresPiercedContact)
				{
					Vector3 normal = diff;
					normal.Normalize();

					EnemyBall* myEnemy =
						GetGameObject()->GetComponent<EnemyBall>();
					EnemyBall* otherEnemy =
						other->GetGameObject()->GetComponent<EnemyBall>();
					PlayerBall* myPlayer =
						GetGameObject()->GetComponent<PlayerBall>();
					PlayerBall* otherPlayer =
						other->GetGameObject()->GetComponent<PlayerBall>();

					const bool myPlayerPiercesOther =
						myPlayer != nullptr &&
						otherEnemy != nullptr &&
						HasPierceAbility() &&
						!m_PierceConsumed;
					const bool otherPlayerPiercesMe =
						otherPlayer != nullptr &&
						myEnemy != nullptr &&
						other->HasPierceAbility() &&
						!otherCollision->m_PierceConsumed;
					const bool piercesThisCollision =
						myPlayerPiercesOther ||
						otherPlayerPiercesMe;

					if (!piercesThisCollision)
					{
						float overlap = minDist - distance;
						Translate(normal * (overlap * 0.5f));
						other->Translate(-normal * (overlap * 0.5f));
					}

					float myDot = Collision::Dot(m_PhysicsComponent->Velocity(), normal);
					float otherDot = Collision::Dot(otherCollision->m_PhysicsComponent->Velocity(), normal);

					if (myDot - otherDot < 0)
					{
						if (piercesThisCollision)
						{
							constexpr float PierceSpeedRetention = 0.75f;
							if (myPlayerPiercesOther)
							{
								m_PierceConsumed = true;
								m_PiercedBall = other;
								m_PhysicsComponent->Velocity() *=
									PierceSpeedRetention;
								stepVelocity =
									m_PhysicsComponent->Velocity() /
									static_cast<float>(subSteps);
							}
							else
							{
								otherCollision->m_PierceConsumed = true;
								otherCollision->m_PiercedBall = m_BallComponent;
								otherCollision->m_PhysicsComponent->Velocity() *=
									PierceSpeedRetention;
							}
						}
						else
						{
							float restitution =
								m_PhysicsComponent->Restitution();

							// 質量を考慮した速度変化量
							float totalMass =
								m_PhysicsComponent->Mass() +
								otherCollision->m_PhysicsComponent->Mass();
							float myRatio =
								(2.0f * otherCollision->m_PhysicsComponent->Mass()) /
								totalMass;
							float otherRatio =
								(2.0f * m_PhysicsComponent->Mass()) /
								totalMass;

							m_PhysicsComponent->Velocity() -=
								normal * myRatio * (myDot - otherDot) *
								restitution;
							otherCollision->m_PhysicsComponent->Velocity() +=
								normal * otherRatio * (myDot - otherDot) *
								restitution;

							stepVelocity =
								m_PhysicsComponent->Velocity() /
								static_cast<float>(subSteps);
						}

						// ==========================================================
						// ==========================================================
						// Apply collision damage
						// ==========================================================
						int damageToThis = other->GetAttack();
						int damageToOther = GetAttack();

						const bool isPlayerEnemyCollision =
							(myPlayer != nullptr && otherEnemy != nullptr) ||
							(myEnemy != nullptr && otherPlayer != nullptr);
						const bool isEnemyEnemyCollision =
							myEnemy != nullptr && otherEnemy != nullptr;
						if (isPlayerEnemyCollision)
						{
							EnemyBall* bankShotTarget =
								myPlayer != nullptr ? otherEnemy : myEnemy;
							if (bankShotTarget != nullptr &&
								!bankShotTarget->IsDefeated())
							{
								const int bankShotMultiplier =
									Game::GetInstance()->
										ConsumeBankShotDamageMultiplier();
								if (myPlayer != nullptr)
								{
									damageToOther *= bankShotMultiplier;
								}
								else
								{
									damageToThis *= bankShotMultiplier;
								}
							}
						}
						if (isEnemyEnemyCollision)
						{
							const int impactBonus =
								Game::GetInstance()->
									GetCurrentShotCollisionAttackBonus();
							damageToThis += impactBonus;
							damageToOther += impactBonus;
						}
						const bool myEnemyWasFullHp =
							myEnemy != nullptr &&
							otherPlayer != nullptr &&
							myEnemy->GetHP() == myEnemy->GetMaxHP();
						const bool otherEnemyWasFullHp =
							otherEnemy != nullptr &&
							myPlayer != nullptr &&
							otherEnemy->GetHP() == otherEnemy->GetMaxHP();

						if (isPlayerEnemyCollision)
						{
							BalanceLogger::GetInstance().RecordDamageCollision(
								BalanceCollisionType::PlayerEnemy);
							Game::GetInstance()->NotifyDynamicBalanceHit();
						}
						else if (isEnemyEnemyCollision)
						{
							// 両方の敵にダメージが入っても、衝突回数は1回。
							BalanceLogger::GetInstance().RecordDamageCollision(
								BalanceCollisionType::EnemyEnemy);
							Game::GetInstance()->NotifyDynamicBalanceHit();
						}

						if (myEnemy != nullptr)
						{
							myEnemy->TakeDamage(damageToThis);
							if (myEnemyWasFullHp &&
								!myEnemy->IsDefeated())
							{
								Game::GetInstance()->
									NotifyBalanceAutoFullHpEnemySurvived();
							}
						}

						if (otherEnemy != nullptr)
						{
							otherEnemy->TakeDamage(damageToOther);
							if (otherEnemyWasFullHp &&
								!otherEnemy->IsDefeated())
							{
								Game::GetInstance()->
									NotifyBalanceAutoFullHpEnemySurvived();
							}
						}

						// Anchor balls transfer the collision first, then stop at
						// the resolved contact position.
						if (isPlayerEnemyCollision)
						{
							if (myPlayer != nullptr &&
								m_BallComponent->HasAnchorAbility())
							{
								m_PhysicsComponent->Velocity() = Vector3::Zero;
								m_PhysicsComponent->Acceleration() = Vector3::Zero;
								stepVelocity = Vector3::Zero;
							}
							else if (otherPlayer != nullptr &&
								other->HasAnchorAbility())
							{
								otherCollision->m_PhysicsComponent->Velocity() =
									Vector3::Zero;
								otherCollision->m_PhysicsComponent->Acceleration() =
									Vector3::Zero;
							}
						}

						if (isPlayerEnemyCollision)
						{
							Game::GetInstance()->
								NotifyDamageBallCollision(
									DamageBallCollisionType::PlayerEnemy);
						}
						else if (isEnemyEnemyCollision)
						{
							Game::GetInstance()->
								NotifyDamageBallCollision(
									DamageBallCollisionType::EnemyEnemy);
						}
						// ==========================================================
					}
				}
			}

			// Wall and ball collision resolution can change the position after
			// the first trigger check, so verify the completed substep as well.
			if (CheckPocketHitAlongMovement(movementStart))
			{
				return;
			}
		}
	}
	else
	{
		// Groundが無い時の保険
		const Vector3 movementStart = GetPosition();
		Translate(m_PhysicsComponent->Velocity());
		if (CheckPocketHitAlongMovement(movementStart))
		{
			return;
		}
	}
}

void BallCollisionComponent::ResetShotAbilityState()
{
	m_PierceConsumed = false;
	m_PiercedBall = nullptr;
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
	const Vector3& movementStart)
{
	if (m_BallComponent == nullptr)
	{
		return false;
	}
	// A normally defeated enemy remains physical until the shot stops and can
	// still fall into a pocket. A defeated player does not need pocket logic.
	if (m_BallComponent->IsDefeated() &&
		GetGameObject()->GetComponent<EnemyBall>() == nullptr)
	{
		return false;
	}

	Vector3 segmentStart = movementStart;
	Vector3 segmentEnd = GetPosition();
	const std::vector<Pocket*> pockets =
		Game::GetInstance()->GetComponents<Pocket>();
	for (Pocket* pocket : pockets)
	{
		if (pocket == nullptr || pocket->GetGameObject() == nullptr ||
			!pocket->GetGameObject()->IsActive())
		{
			continue;
		}

		const Collision::Sphere pocketSphere = pocket->GetSphere();
		// Pocket gameplay is two-dimensional. Project the swept segment to
		// the table height so visual/model Y offsets cannot suppress a hit.
		segmentStart.y = pocketSphere.center.y;
		segmentEnd.y = pocketSphere.center.y;
		const float triggerRadius =
			GetSphere().radius + pocketSphere.radius;
		const Collision::Segment sweptMovement{
			segmentStart,
			segmentEnd
		};
		if (Collision::DistanceSquaredPointToSegment(
			pocketSphere.center,
			sweptMovement) <= triggerRadius * triggerRadius)
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
