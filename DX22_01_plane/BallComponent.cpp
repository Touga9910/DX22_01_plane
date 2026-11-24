#include "BallComponent.h"

#include "BallCollisionComponent.h"
#include "imgui/imgui.h"
#include "GameObject.h"
#include "TransformComponent.h"

#include <algorithm>

using namespace DirectX::SimpleMath;

/// <summary>
/// ボールが使う各コンポーネントを取得し、半径と衝突処理の初期状態を設定する。
/// </summary>
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

/// <summary>
/// 変換コンポーネントの位置を返し、未設定時は原点を返す。
/// </summary>
Vector3 BallComponent::GetPosition() const
{
	const TransformComponent* transform = GetTransform();
	return transform != nullptr
		? transform->GetPosition()
		: Vector3::Zero;
}

/// <summary>
/// 変換コンポーネントの回転を返し、未設定時は単位クォータニオンを返す。
/// </summary>
Quaternion BallComponent::GetRotation() const
{
	const TransformComponent* transform = GetTransform();
	return transform != nullptr
		? transform->GetRotation()
		: Quaternion::Identity;
}

/// <summary>
/// 変換コンポーネントの拡大率を返し、未設定時は各軸の拡大率を1として扱う。
/// </summary>
Vector3 BallComponent::GetScale() const
{
	const TransformComponent* transform = GetTransform();
	return transform != nullptr
		? transform->GetScale()
		: Vector3::One;
}

/// <summary>
/// 変換コンポーネントがあればボールの位置を更新する。
/// </summary>
void BallComponent::SetPosition(const Vector3& position)
{
	if (TransformComponent* transform = GetTransform())
	{
		transform->SetPosition(position);
	}
}

/// <summary>
/// 変換コンポーネントを通してボールに指定移動量を加える。
/// </summary>
void BallComponent::Translate(const Vector3& movement)
{
	if (TransformComponent* transform = GetTransform())
	{
		transform->Translate(movement);
	}
}

/// <summary>
/// 変換コンポーネントがあればボールの回転を更新する。
/// </summary>
void BallComponent::SetRotation(const Quaternion& rotation)
{
	if (TransformComponent* transform = GetTransform())
	{
		transform->SetRotation(rotation);
	}
}

/// <summary>
/// 変換コンポーネントがあればボールの拡大率を設定する。
/// </summary>
void BallComponent::SetScale(const Vector3& scale)
{
	if (TransformComponent* transform = GetTransform())
	{
		transform->SetScale(scale);
	}
}

/// <summary>
/// ボールのステータス全体を設定し、質量や反発係数などを物理側に反映する。
/// </summary>
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

/// <summary>
/// 現在HPを設定し、関連コンポーネントの状態を同期する。
/// </summary>
void BallComponent::SetHP(int hp)
{
	m_StatusComponent->SetCurrentHp(hp);
	SynchronizeComponents();
}

/// <summary>
/// 最大HPを設定し、関連コンポーネントの状態を同期する。
/// </summary>
void BallComponent::SetMaxHP(int maxHp)
{
	m_StatusComponent->SetMaxHp(maxHp);
	SynchronizeComponents();
}

/// <summary>
/// 撃破フラグを解除し、関連コンポーネントの状態を同期する。
/// </summary>
void BallComponent::ResetDefeated()
{
	m_StatusComponent->ResetDefeated();
	SynchronizeComponents();
}

/// <summary>
/// 衝突コンポーネントが保持するショット中の能力状態を初期化する。
/// </summary>
void BallComponent::ResetShotAbilityState()
{
	if (m_BallCollisionComponent != nullptr)
	{
		m_BallCollisionComponent->ResetShotAbilityState();
	}
}

/// <summary>
/// 指定したステータス値を適用し、関連コンポーネントを同期する。
/// </summary>
void BallComponent::ApplyStatusValuesOnly(const BallStatus& status)
{
	ApplyStatusValues(status);
	SynchronizeComponents();
}

/// <summary>
/// ステータス値を更新し、質量・反発・摩擦と半径を物理側に反映する。
/// </summary>
void BallComponent::ApplyStatusValues(const BallStatus& status)
{
	m_StatusComponent->ApplyStatusValuesOnly(status);
	const BallStatus& applied = GetStatus();
	m_PhysicsComponent->Mass() = applied.mass;
	m_PhysicsComponent->Restitution() = applied.restitution;
	m_PhysicsComponent->Friction() = applied.friction;
	UpdateRadius();
}

/// <summary>
/// 衝突半径に非負の値を設定し、ステータス・物理・コライダー間で同期する。
/// </summary>
void BallComponent::SetRadius(float radius)
{
	BallStatus status = GetStatus();
	status.radius = (std::max)(0.0f, radius);
	m_StatusComponent->ApplyStatusValuesOnly(status);
	m_PhysicsComponent->Radius() = GetStatus().radius;
	SynchronizeComponents();
}

/// <summary>
/// 明示された半径を優先し、未指定なら描画モデルの寸法と拡大率から半径を求める。
/// </summary>
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

/// <summary>
/// コライダーの半径を物理コンポーネントの半径に合わせる。
/// </summary>
void BallComponent::SynchronizeComponents()
{
	if (m_ColliderComponent != nullptr)
	{
		m_ColliderComponent->SetRadius(m_PhysicsComponent->Radius());
	}
}

/// <summary>
/// 未撃破のボールに防御補正込みのダメージを適用し、HPが尽きたら撃破処理を行う。
/// </summary>
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

/// <summary>
/// 防御計算後の正のダメージを与え、HPが尽きたら撃破処理を行う。
/// </summary>
void BallComponent::DamageAfterDefense(int damage)
{
	if (IsDefeated() || damage <= 0)
	{
		return;
	}
	if (m_StatusComponent->ApplyFinalDamage(damage))
	{
		Defeat();
	}
}

/// <summary>
/// 外部から受け取ったダメージを共通のダメージ処理へ渡す。
/// </summary>
void BallComponent::TakeDamage(int damage)
{
	// 外部から受け取ったダメージを、共通のダメージ処理へ渡す
	Damage(damage);
}

/// <summary>
/// ボールを撃破状態にして移動を止め、撃破時の処理を呼び出す。
/// </summary>
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

/// <summary>
/// 物理ステップ開始時の位置を保存し、速度のY成分をゼロにする。
/// </summary>
void BallComponent::BeginPhysicsStep()
{
	m_PhysicsComponent->OldPosition() = GetPosition();
	m_PhysicsComponent->Velocity().y = 0.0f;
}

/// <summary>
/// 物理ステップ中の水平移動からボールの転がり回転を更新する。
/// </summary>
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

/// <summary>
/// ボールの位置と運動状態を初期配置に戻す。
/// </summary>
void BallComponent::ResetToInitialPosition()
{
	ResetAtPosition(m_PhysicsComponent->InitialPosition());
}

/// <summary>
/// ボールを指定位置へ戻し、速度・加速度・転がりとショット能力を初期化する。
/// </summary>
void BallComponent::ResetAtPosition(const Vector3& position)
{
	ResetShotAbilityState();
	SetPosition(position);
	m_PhysicsComponent->OldPosition() = position;
	m_PhysicsComponent->Velocity() = Vector3::Zero;
	m_PhysicsComponent->Acceleration() = Vector3::Zero;
	m_PhysicsComponent->RollingRotation() = Quaternion::Identity;
}

/// <summary>
/// ポケットに入ったときの専用処理を呼び、未登録時は速度と加速度を止める。
/// </summary>
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

/// <summary>
/// 所有するゲームオブジェクトの破棄を要求する。
/// </summary>
void BallComponent::Destroy()
{
	if (GameObject* owner = GetGameObject())
	{
		owner->Destroy();
	}
}

/// <summary>
/// ボールの拡大率などを確認・調整できるImGuiのデバッグ欄を描画する。
/// </summary>
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
