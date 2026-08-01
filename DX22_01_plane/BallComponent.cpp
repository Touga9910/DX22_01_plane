#include "BallComponent.h"

#include "Collision.h"
#include "BalanceLogger.h"
#include "Game.h"
#include "TableFrame.h"
#include "Pocket.h"
#include "imgui/imgui.h"
#include "EnemyBall.h"
#include "PlayerBall.h"
#include "GameObject.h"
#include "TransformComponent.h"

#include <algorithm>
#include <cmath>

using namespace std;
using namespace DirectX::SimpleMath;

void BallComponent::Awake()
{
	GameObject* owner = GetGameObject();
	if (owner == nullptr) return;

	m_StatusComponent = owner->AddComponent<BallStatusComponent>();
	m_PhysicsComponent = owner->AddComponent<BallPhysicsComponent>();
	m_ColliderComponent = owner->AddComponent<SphereColliderComponent>(m_PhysicsComponent->Radius(), false);
	m_RenderComponent = owner->AddComponent<BallRenderComponent>();
	SynchronizeComponents();
}

void BallComponent::LateUpdate()
{
	SynchronizeComponents();
}

void BallComponent::SetStatus(const BallStatus& status)
{
	m_StatusComponent->SetStatus(status);
	const BallStatus& applied = GetStatus();
	m_PhysicsComponent->Mass() = applied.mass;
	m_PhysicsComponent->Restitution() = applied.restitution;
	m_PhysicsComponent->Friction() = applied.friction;
	UpdateRadius();
	SynchronizeComponents();
}

void BallComponent::SetHP(int hp)
{
	m_StatusComponent->SetCurrentHp(hp);
	SynchronizeComponents();
}

void BallComponent::SetMaxHP(int maxHp)
{
	m_StatusComponent->SetMaxHp(maxHp);
	SynchronizeComponents();
}

void BallComponent::ResetDefeated()
{
	m_StatusComponent->ResetDefeated();
	SynchronizeComponents();
}

void BallComponent::ApplyStatusValuesOnly(const BallStatus& status)
{
	ApplyStatusValues(status);
	SynchronizeComponents();
}

void BallComponent::ApplyStatusValues(const BallStatus& status)
{
	m_StatusComponent->ApplyStatusValuesOnly(status);
	const BallStatus& applied = GetStatus();
	m_PhysicsComponent->Mass() = applied.mass;
	m_PhysicsComponent->Restitution() = applied.restitution;
	m_PhysicsComponent->Friction() = applied.friction;
	UpdateRadius();
}

void BallComponent::SetRadius(float radius)
{
	BallStatus status = GetStatus();
	status.radius = (std::max)(0.0f, radius);
	m_StatusComponent->ApplyStatusValuesOnly(status);
	m_PhysicsComponent->Radius() = GetStatus().radius;
	SynchronizeComponents();
}

void BallComponent::UpdateRadius()
{
	if (GetStatus().radius > 0.0f)
	{
		m_PhysicsComponent->Radius() = GetStatus().radius;
	}
	else
	{
		const float maxScale = (std::max)({
			m_Transform.scale.x,
			m_Transform.scale.y,
			m_Transform.scale.z });
		const float modelRadius = m_RenderComponent == nullptr
			? 1.0f
			: m_RenderComponent->GetModelBaseRadius();
		m_PhysicsComponent->Radius() = modelRadius * maxScale;
	}

	SynchronizeComponents();
}

void BallComponent::SynchronizeComponents()
{
	if (TransformComponent* transform = GetTransform())
	{
		transform->SetPosition(m_Transform.position);
		transform->SetRotation(Quaternion::CreateFromYawPitchRoll(
			m_Transform.rotation.y,
			m_Transform.rotation.x,
			m_Transform.rotation.z));
		transform->SetScale(m_Transform.scale);
	}

	if (m_ColliderComponent != nullptr)
	{
		m_ColliderComponent->SetRadius(m_PhysicsComponent->Radius());
	}
}

void BallComponent::Damage(int damage)
{
	if (IsDefeated())
	{
		return;
	}

	if (m_StatusComponent->ApplyDamage(damage))
	{
		Defeat();
	}
}

void BallComponent::TakeDamage(int damage)
{
	// 外部から受け取ったダメージを、共通のダメージ処理へ渡す
	Damage(damage);
}

void BallComponent::Defeat()
{
	if (IsDefeated())
	{
		return;
	}

	m_StatusComponent->MarkDefeated();

	// 倒されたら速度を止める
	m_PhysicsComponent->Velocity() = DirectX::SimpleMath::Vector3::Zero;
	m_PhysicsComponent->Acceleration() = DirectX::SimpleMath::Vector3::Zero;
	SynchronizeComponents();

	if (m_DefeatHandler)
	{
		m_DefeatHandler();
	}
}

void BallComponent::UpdatePhysics()
{
	m_PhysicsComponent->OldPosition() = m_Transform.position;

	// 壁の当たり判定についての処理（動的サブステップ方式）//

	// Y方向（上下）には絶対に動かないようにする
	m_PhysicsComponent->Velocity().y = 0.0f;

	std::vector<Collision::Segment> walls;                          // テーブル枠から集めた壁情報
	std::vector<TableFrame*> frames = Game::GetInstance()->GetObjects<TableFrame>();

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
		// 1フレームの移動距離を計算
		float moveDistance = m_PhysicsComponent->Velocity().Length();

		// 1ステップで進んでいい最大の距離（すり抜けないよう半径の半分以下にする）
		float maxStep = m_PhysicsComponent->Radius() * 0.5f;

		// 必要な分割数（速度が遅ければ1回、速ければ自動で増える）
		int subSteps = (std::max)(1, static_cast<int>(std::ceil(moveDistance / maxStep)));

		std::vector<BallComponent*> balls = Game::GetInstance()->GetObjects<BallComponent>();
		for (BallComponent* other : balls)
		{
			if (other == this) continue;

			Vector3 relativeVelocity = m_PhysicsComponent->Velocity() - other->m_PhysicsComponent->Velocity();
			float relativeSpeed = relativeVelocity.Length();

			int relativeSubSteps = (std::max)(1, static_cast<int>(std::ceil(relativeSpeed / maxStep)));
			subSteps = (std::max)(subSteps, relativeSubSteps);
		}

		// 1ステップあたりの移動量
		Vector3 stepVelocity = m_PhysicsComponent->Velocity() / static_cast<float>(subSteps);

		for (int step = 0; step < subSteps; step++)
		{
			// 少しだけ移動させる
			m_Transform.position += stepVelocity;

			// その位置で壁との当たり判定
			for (int i = 0; i < static_cast<int>(walls.size()); i++)
			{
				Vector3 contactPoint;
				float distance = Collision::DistancePointToSegment(m_Transform.position, walls[i], contactPoint);

				// 距離が半径以下なら衝突
				if (distance <= m_PhysicsComponent->Radius())
				{
					// 法線の計算
					Vector3 normal = m_Transform.position - contactPoint;
					normal.y = 0.0f;

					if (normal.LengthSquared() > 0.0001f)
					{
						normal.Normalize();
					}
					else
					{
						// 万が一完全に重なった場合の安全装置
						Vector3 wallVec = walls[i].end - walls[i].start;
						wallVec.Normalize();
						normal = Vector3(-wallVec.z, 0.0f, wallVec.x);
					}

					// 進行方向と法線が逆向きになるよう調整
					if (Collision::Dot(m_PhysicsComponent->Velocity(), normal) > 0)
					{
						normal = -normal;
					}

					// 1. めり込み防止（衝突点から半径分押し返す）
					m_Transform.position = contactPoint + normal * m_PhysicsComponent->Radius();

					// 2. 反射処理
					float dot = Collision::Dot(m_PhysicsComponent->Velocity(), normal);
					if (dot < 0)
					{
						float restitution = m_PhysicsComponent->Restitution(); // 反発係数
						m_PhysicsComponent->Velocity() = m_PhysicsComponent->Velocity() - normal * (2.0f * dot) * restitution;

						// 反射したので、残りのステップの移動方向も「反射後の速度」に更新する
						stepVelocity = m_PhysicsComponent->Velocity() / static_cast<float>(subSteps);
					}
				}
			}

			// ボール同士の衝突判定（二重処理防止版）
			std::vector<BallComponent*> balls = Game::GetInstance()->GetObjects<BallComponent>();
			bool foundSelf = false;  // 自分を見つけたかのフラグ

			for (BallComponent* other : balls)
			{
				// 自分を見つけるまでスキップ
				if (!foundSelf)
				{
					if (other == this) foundSelf = true;
					continue;  // 自分自身も含めてスキップ
				}

				// ↓ ここから「自分より後ろのボール」とだけ判定される
				Vector3 diff = m_Transform.position - other->m_Transform.position;
				float distance = diff.Length();
				float minDist = m_PhysicsComponent->Radius() + other->m_PhysicsComponent->Radius();

				if (distance >= minDist)
				{
					if (m_PiercedBall == other)
					{
						m_PiercedBall = nullptr;
					}
					if (other->m_PiercedBall == this)
					{
						other->m_PiercedBall = nullptr;
					}
				}

				const bool ignoresPiercedContact =
					m_PiercedBall == other ||
					other->m_PiercedBall == this;

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
						!other->m_PierceConsumed;
					const bool piercesThisCollision =
						myPlayerPiercesOther ||
						otherPlayerPiercesMe;

					if (!piercesThisCollision)
					{
						float overlap = minDist - distance;
						m_Transform.position += normal * (overlap * 0.5f);
						other->m_Transform.position -= normal * (overlap * 0.5f);
					}

					float myDot = Collision::Dot(m_PhysicsComponent->Velocity(), normal);
					float otherDot = Collision::Dot(other->m_PhysicsComponent->Velocity(), normal);

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
								other->m_PierceConsumed = true;
								other->m_PiercedBall = this;
								other->m_PhysicsComponent->Velocity() *=
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
								other->m_PhysicsComponent->Mass();
							float myRatio =
								(2.0f * other->m_PhysicsComponent->Mass()) /
								totalMass;
							float otherRatio =
								(2.0f * m_PhysicsComponent->Mass()) /
								totalMass;

							m_PhysicsComponent->Velocity() -=
								normal * myRatio * (myDot - otherDot) *
								restitution;
							other->m_PhysicsComponent->Velocity() +=
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
		}
	}
	else
	{
		// Groundが無い時の保険
		m_Transform.position += m_PhysicsComponent->Velocity();
	}

	// ==========================
	// ポケットとの当たり判定
	// ==========================
	std::vector<Pocket*> pockets = Game::GetInstance()->GetObjects<Pocket>();

	for (Pocket* pocket : pockets)
	{
		if (Collision::CheckHit(GetSphere(), pocket->GetSphere()))
		{
			OnPocketHit();
			break;
		}
	}

	// ==========================
	// ボールの転がり回転の計算
	// ==========================
	if (m_PhysicsComponent->OldPosition() != m_Transform.position)
	{
		// 1フレームの実際の移動ベクトル
		Vector3 moveVec = m_Transform.position - m_PhysicsComponent->OldPosition();

		// Y軸のブレによる影響を消すため、水平方向の移動のみを考慮
		moveVec.y = 0.0f;

		float distance = moveVec.Length();

		// 移動している場合のみ回転処理（ゼロ除算防止）
		if (distance > 0.0001f)
		{
			// 移動方向（正規化ベクトル）
			Vector3 moveDir = moveVec / distance;

			// 回転軸の計算（外積）
			// 進行方向(moveDir)と真上(UnitY)の外積をとることで、進行方向に対して「真横」の軸を取得
			Vector3 rotationAxis = Vector3::UnitY.Cross(moveDir);
			rotationAxis.Normalize();

			// 回転角の計算
			float angle = distance / m_PhysicsComponent->Radius();

			// 指定した軸(rotationAxis)を中心に、指定した角度(angle)だけ回転するクォータニオン
			Quaternion deltaRot = Quaternion::CreateFromAxisAngle(rotationAxis, angle);

			// 5. 現在の転がり回転に掛け合わせる
			//m_PhysicsComponent->RollingRotation() = deltaRot * m_PhysicsComponent->RollingRotation();
			m_PhysicsComponent->RollingRotation() = m_PhysicsComponent->RollingRotation() * deltaRot;
		}
	}
}

void BallComponent::ResetToInitialPosition()
{
	ResetShotAbilityState();

	// 位置情報を初期位置に戻す
	m_Transform.position = m_PhysicsComponent->InitialPosition();
	m_PhysicsComponent->OldPosition() = m_PhysicsComponent->InitialPosition();

	// 移動・加速度・転がり回転をリセットする
	m_PhysicsComponent->Velocity() = DirectX::SimpleMath::Vector3::Zero;
	m_PhysicsComponent->Acceleration() = DirectX::SimpleMath::Vector3::Zero;
	m_PhysicsComponent->RollingRotation() = DirectX::SimpleMath::Quaternion::Identity;
}

void BallComponent::OnPocketHit()
{
	if (m_PocketHandler)
	{
		m_PocketHandler();
		return;
	}

	m_PhysicsComponent->Velocity() = DirectX::SimpleMath::Vector3::Zero;
	m_PhysicsComponent->Acceleration() = DirectX::SimpleMath::Vector3::Zero;
}

void BallComponent::LoadModel(const char* modelFilePath, const char* texDirectory)
{
	if (m_RenderComponent == nullptr) return;
	m_RenderComponent->LoadModel(modelFilePath, texDirectory);
	UpdateRadius();
}

void BallComponent::BeginDraw()
{
	if (m_RenderComponent != nullptr) m_RenderComponent->BeginDraw();
}

void BallComponent::DrawMesh(const DirectX::SimpleMath::Matrix& worldMtx)
{
	if (m_RenderComponent != nullptr) m_RenderComponent->DrawMesh(worldMtx);
}

void BallComponent::Destroy()
{
	if (GameObject* owner = GetGameObject())
	{
		owner->Destroy();
	}
}

void BallComponent::DrawImGui(const std::string& label)
{
	ImGui::PushID(label.c_str());

	if (ImGui::CollapsingHeader(label.c_str()))
	{
		// スケール
		float currentScale = m_Transform.scale.x;

		if (ImGui::SliderFloat("Ball Scale", &currentScale, 0.1f, 5.0f))
		{
			// スライダーが動いたら、縦横奥（X, Y, Z）全てのスケールを均等に更新
			m_Transform.scale.x = currentScale;
			m_Transform.scale.y = currentScale;
			m_Transform.scale.z = currentScale;

			// スケールが変わったので、これに連動して物理半径 m_PhysicsComponent->Radius() を再計算する
			UpdateRadius();
		}

		// 質量
		ImGui::SliderFloat("Mass", &m_PhysicsComponent->Mass(), 0.1f, 10.0f);

		// 速度（読み取り専用で表示）
		ImGui::Text("Velocity: (%.2f, %.2f, %.2f)",
			m_PhysicsComponent->Velocity().x, m_PhysicsComponent->Velocity().y, m_PhysicsComponent->Velocity().z);

		// 座標（読み取り専用で表示）
		ImGui::Text("Position: (%.2f, %.2f, %.2f)",
			m_Transform.position.x,
			m_Transform.position.y,
			m_Transform.position.z);

		// ステータス状態
		ImGui::Text("HP: %d / %d", GetHP(), GetMaxHP());
		ImGui::Text("Attack: %d", GetAttack());
		ImGui::Text("Defense: %d", GetDefense());

		// 反発係数
		ImGui::SliderFloat("Restitution", &m_PhysicsComponent->Restitution(), 0.0f, 1.0f);

		// 摩擦係数
		ImGui::SliderFloat("Friction", &m_PhysicsComponent->Friction(), 0.0f, 1.0f);
	}

	ImGui::PopID();
}
