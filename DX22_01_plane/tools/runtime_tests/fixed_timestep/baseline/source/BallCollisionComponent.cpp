#include "BallCollisionComponent.h"

#include "BalanceLogger.h"
#include "BallComponent.h"
#include "BallMechanics.h"
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
	const std::vector<Pocket*> pockets =
		Game::GetInstance()->GetComponents<Pocket>();

	// 別のボールが先に衝突を解決した結果、すでにポケットと重なっている場合がある。
	// このフレームの移動を適用する前に、その状態を処理する。
	if (CheckPocketHitAlongMovement(GetPosition(), pockets))
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

		const std::vector<BallComponent*> balls =
			Game::GetInstance()->GetComponents<BallComponent>();
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
			// フレーム終端の位置だけでなく、移動中に通過した経路も調べる。
			// これにより、高速なボールがポケットを完全にすり抜けることを防ぐ。
			if (CheckPocketHitAlongMovement(movementStart, pockets))
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
					std::erase(m_PiercedBalls, other);
					std::erase(otherCollision->m_PiercedBalls, m_BallComponent);
				}
				const bool ignoresPiercedContact =
					std::find(m_PiercedBalls.begin(), m_PiercedBalls.end(), other) != m_PiercedBalls.end() ||
					std::find(otherCollision->m_PiercedBalls.begin(), otherCollision->m_PiercedBalls.end(), m_BallComponent) != otherCollision->m_PiercedBalls.end();

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
						m_PierceUseCount <
							Game::GetInstance()->GetPierceMaximumUses();
					const bool otherPlayerPiercesMe =
						otherPlayer != nullptr &&
						myEnemy != nullptr &&
						other->HasPierceAbility() &&
						otherCollision->m_PierceUseCount <
							Game::GetInstance()->GetPierceMaximumUses();
					const bool piercesThisCollision =
						myPlayerPiercesOther ||
						otherPlayerPiercesMe;

					// 停止済みアンカーは位置も速度も押し戻されない。
					const bool myAnchorLocked = myPlayer != nullptr &&
						m_BallComponent->HasAnchorAbility() && m_BallComponent->GetStatus().anchorKnockbackImmune &&
						m_PhysicsComponent->Velocity().LengthSquared() <= 0.0001f;
					const bool otherAnchorLocked = otherPlayer != nullptr &&
						other->HasAnchorAbility() && other->GetStatus().anchorKnockbackImmune &&
						otherCollision->m_PhysicsComponent->Velocity().LengthSquared() <= 0.0001f;

					if (!piercesThisCollision)
					{
						float overlap = minDist - distance;
						if (!myAnchorLocked) Translate(normal * overlap * (otherAnchorLocked ? 1.0f : 0.5f));
						if (!otherAnchorLocked) other->Translate(-normal * overlap * (myAnchorLocked ? 1.0f : 0.5f));
					}

					float myDot = Collision::Dot(m_PhysicsComponent->Velocity(), normal);
					float otherDot = Collision::Dot(otherCollision->m_PhysicsComponent->Velocity(), normal);

					if (myDot - otherDot < 0)
					{
						if (piercesThisCollision)
						{
							const float pierceSpeedRetention =
								Game::GetInstance()->GetPierceSpeedRetention();
							if (myPlayerPiercesOther)
							{
								m_PierceUseCount++;
								m_PiercedBalls.push_back(other);
								m_PhysicsComponent->Velocity() *=
									pierceSpeedRetention;
								stepVelocity =
									m_PhysicsComponent->Velocity() /
									static_cast<float>(subSteps);
							}
							else
							{
								otherCollision->m_PierceUseCount++;
								otherCollision->m_PiercedBalls.push_back(m_BallComponent);
								otherCollision->m_PhysicsComponent->Velocity() *=
									pierceSpeedRetention;
							}
						}
						else
						{
							float restitution =
								myPlayer != nullptr ? m_PhysicsComponent->Restitution() :
								(otherPlayer != nullptr ? otherCollision->m_PhysicsComponent->Restitution() :
								m_PhysicsComponent->Restitution());

							const float myTransfer = myPlayer != nullptr && otherEnemy != nullptr
								? m_BallComponent->GetStatus().knockbackTransfer : 1.0f;
							const float otherTransfer = otherPlayer != nullptr && myEnemy != nullptr
								? other->GetStatus().knockbackTransfer : 1.0f;
							const auto response = BallMechanics::ResolveNormalImpact(
								myDot, otherDot, m_PhysicsComponent->Mass(), otherCollision->m_PhysicsComponent->Mass(),
								restitution, myTransfer, otherTransfer, myAnchorLocked, otherAnchorLocked);
							m_PhysicsComponent->Velocity() += normal * (response.first - myDot);
							otherCollision->m_PhysicsComponent->Velocity() += normal * (response.second - otherDot);
							stepVelocity =
								m_PhysicsComponent->Velocity() /
								static_cast<float>(subSteps);
						}

						// Physics separation remains active in every state, but a
						// contact is an attack only while a shot is being resolved.
						if (Game::GetInstance()->GetGameState() !=
							GameState::BallsMoving)
						{
							continue;
						}

						// ==========================================================
						// ==========================================================
						// 衝突ダメージを適用する
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
								const int relicDamageBonus =
									Game::GetInstance()->
										ConsumePlayerEnemyRelicDamageBonus();
								if (myPlayer != nullptr)
								{
									damageToOther += relicDamageBonus;
								}
								else
								{
									damageToThis += relicDamageBonus;
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
							myEnemy->TakeDamage(myEnemy->AdjustCollisionDamage(damageToThis, other->GetPosition()));
							const int appliedDamage = (std::max)(
								0,
								myEnemyHpBefore - myEnemy->GetHP());
							if (!myEnemyWasDefeated &&
								(appliedDamage > 0 || myEnemy->IsDefeated()))
							{
								Game::GetInstance()->NotifyCombatFeedback(
									myEnemyFeedbackPosition,
									appliedDamage,
									myEnemy->IsDefeated(),
									isEnemyEnemyCollision);
							}
							if (myEnemyWasFullHp &&
								!myEnemy->IsDefeated())
							{
								Game::GetInstance()->
									NotifyBalanceAutoFullHpEnemySurvived();
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
								Game::GetInstance()->NotifyCombatFeedback(
									otherEnemyFeedbackPosition,
									appliedDamage,
									otherEnemy->IsDefeated(),
									isEnemyEnemyCollision);
							}
							if (otherEnemyWasFullHp &&
								!otherEnemy->IsDefeated())
							{
								Game::GetInstance()->
									NotifyBalanceAutoFullHpEnemySurvived();
							}
						}

						// アンカー球は先に衝突の力を相手へ伝え、
						// 接触解決後の位置で停止する。
						if (isPlayerEnemyCollision)
						{
							if (myPlayer != nullptr &&
								m_BallComponent->HasAnchorAbility())
							{
								m_PhysicsComponent->Velocity() = Vector3::Zero;
								m_PhysicsComponent->Acceleration() = Vector3::Zero;
								stepVelocity = Vector3::Zero;
								Game::GetInstance()->NotifyAnchorStopped();
							}
							else if (otherPlayer != nullptr &&
								other->HasAnchorAbility())
							{
								otherCollision->m_PhysicsComponent->Velocity() =
									Vector3::Zero;
								otherCollision->m_PhysicsComponent->Acceleration() =
									Vector3::Zero;
								Game::GetInstance()->NotifyAnchorStopped();
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

			// 壁やボールとの衝突解決によって、最初のトリガー判定後に位置が変わる場合があるため、
			// サブステップ完了後の位置でも再確認する。
			if (CheckPocketHitAlongMovement(movementStart, pockets))
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
		if (CheckPocketHitAlongMovement(movementStart, pockets))
		{
			return;
		}
	}
}

void BallCollisionComponent::ResetShotAbilityState()
{
	m_PierceUseCount = 0;
	m_PiercedBalls.clear();
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

	Vector3 segmentStart = movementStart;
	Vector3 segmentEnd = GetPosition();
	for (Pocket* pocket : pockets)
	{
		if (pocket == nullptr || pocket->GetGameObject() == nullptr ||
			!pocket->GetGameObject()->IsActive())
		{
			continue;
		}

		const Collision::Sphere pocketSphere = pocket->GetSphere();
		// ポケットのゲーム判定は二次元で行う。移動線分をテーブルの高さへ投影し、
		// 描画モデルのY方向のずれによって判定が失われないようにする。
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
