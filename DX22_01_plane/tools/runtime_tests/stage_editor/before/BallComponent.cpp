#include "BallComponent.h"

#include "BallCollisionComponent.h"
#include "imgui/imgui.h"
#include "GameObject.h"
#include "TransformComponent.h"

#include <algorithm>

using namespace DirectX::SimpleMath;

void BallComponent::Awake()
{
	GameObject* owner = GetGameObject();
	if (owner == nullptr) return;

	m_StatusComponent = owner->GetComponent<BallStatusComponent>();
	m_PhysicsComponent = owner->GetComponent<BallPhysicsComponent>();
	m_ColliderComponent = owner->GetComponent<SphereColliderComponent>();
	m_RenderComponent = owner->GetComponent<BallRenderComponent>();
	m_BallCollisionComponent = owner->GetComponent<BallCollisionComponent>();

	if (m_StatusComponent == nullptr ||
		m_PhysicsComponent == nullptr ||
		m_ColliderComponent == nullptr ||
		m_RenderComponent == nullptr ||
		m_BallCollisionComponent == nullptr)
	{
		owner->Destroy();
		return;
	}

	SynchronizeComponents();
}

Vector3 BallComponent::GetPosition() const
{
	const TransformComponent* transform = GetTransform();
	return transform != nullptr
		? transform->GetPosition()
		: Vector3::Zero;
}

Quaternion BallComponent::GetRotation() const
{
	const TransformComponent* transform = GetTransform();
	return transform != nullptr
		? transform->GetRotation()
		: Quaternion::Identity;
}

Vector3 BallComponent::GetScale() const
{
	const TransformComponent* transform = GetTransform();
	return transform != nullptr
		? transform->GetScale()
		: Vector3::One;
}

void BallComponent::SetPosition(const Vector3& position)
{
	if (TransformComponent* transform = GetTransform())
	{
		transform->SetPosition(position);
	}
}

void BallComponent::Translate(const Vector3& movement)
{
	if (TransformComponent* transform = GetTransform())
	{
		transform->Translate(movement);
	}
}

void BallComponent::SetRotation(const Quaternion& rotation)
{
	if (TransformComponent* transform = GetTransform())
	{
		transform->SetRotation(rotation);
	}
}

void BallComponent::SetScale(const Vector3& scale)
{
	if (TransformComponent* transform = GetTransform())
	{
		transform->SetScale(scale);
	}
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

void BallComponent::ResetShotAbilityState()
{
	if (m_BallCollisionComponent != nullptr)
	{
		m_BallCollisionComponent->ResetShotAbilityState();
	}
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
		const Vector3 scale = GetScale();
		const float maxScale = (std::max)({
			scale.x,
			scale.y,
			scale.z });
		const float modelRadius = m_RenderComponent == nullptr
			? 1.0f
			: m_RenderComponent->GetModelBaseRadius();
		m_PhysicsComponent->Radius() = modelRadius * maxScale;
	}

	SynchronizeComponents();
}

void BallComponent::SynchronizeComponents()
{
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

void BallComponent::BeginPhysicsStep()
{
	m_PhysicsComponent->OldPosition() = GetPosition();
	m_PhysicsComponent->Velocity().y = 0.0f;
}

void BallComponent::FinishPhysicsStep()
{
	// ==========================
	// ボールの転がり回転の計算
	// ==========================
	const Vector3 currentPosition = GetPosition();
	if (m_PhysicsComponent->OldPosition() != currentPosition)
	{
		// 1フレームの実際の移動ベクトル
		Vector3 moveVec = currentPosition - m_PhysicsComponent->OldPosition();

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
	ResetAtPosition(m_PhysicsComponent->InitialPosition());
}

void BallComponent::ResetAtPosition(const Vector3& position)
{
	ResetShotAbilityState();
	SetPosition(position);
	m_PhysicsComponent->OldPosition() = position;
	m_PhysicsComponent->Velocity() = Vector3::Zero;
	m_PhysicsComponent->Acceleration() = Vector3::Zero;
	m_PhysicsComponent->RollingRotation() = Quaternion::Identity;
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
		float currentScale = GetScale().x;

		if (ImGui::SliderFloat("Ball Scale", &currentScale, 0.1f, 5.0f))
		{
			// スライダーが動いたら、縦横奥（X, Y, Z）全てのスケールを均等に更新
			SetScale(Vector3(currentScale, currentScale, currentScale));

			// スケールが変わったので、これに連動して物理半径 m_PhysicsComponent->Radius() を再計算する
			UpdateRadius();
		}

		// 質量
		ImGui::SliderFloat("Mass", &m_PhysicsComponent->Mass(), 0.1f, 10.0f);

		// 速度（読み取り専用で表示）
		ImGui::Text("Velocity: (%.2f, %.2f, %.2f)",
			m_PhysicsComponent->Velocity().x, m_PhysicsComponent->Velocity().y, m_PhysicsComponent->Velocity().z);

		// 座標（読み取り専用で表示）
		const Vector3 position = GetPosition();
		ImGui::Text("Position: (%.2f, %.2f, %.2f)",
			position.x,
			position.y,
			position.z);

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
