#include "PierceMinion.h"

#include "BallComponent.h"
#include "BallPhysicsRules.h"
#include "BallRenderComponent.h"
#include "Camera.h"
#include "GameObject.h"
#include "GameRuntime.h"
#include "TableConfig.h"

using namespace DirectX::SimpleMath;

PierceMinion::PierceMinion(
	const BallStatus& status,
	const Vector3& position,
	const Vector3& direction,
	float launchSpeed)
	: m_Status(status),
	  m_Position(position),
	  m_Direction(direction),
	  m_LaunchSpeed(launchSpeed)
{
}

void PierceMinion::Awake()
{
	m_Ball = GetGameObject()->GetComponent<BallComponent>();
	auto* render = GetGameObject()->GetComponent<BallRenderComponent>();
	if (m_Ball == nullptr || render == nullptr)
	{
		GetGameObject()->Destroy();
		return;
	}

	m_Direction.y = 0.0f;
	if (m_Direction.LengthSquared() <= 0.000001f)
	{
		GetGameObject()->Destroy();
		return;
	}
	m_Direction.Normalize();
	m_Position.y = TableConfig::FIELD_HEIGHT;
	m_Ball->SetMaxHP(1);
	m_Ball->SetStatus(m_Status);
	m_Ball->ResetAtPosition(m_Position);
	m_Ball->GetMutableVelocity() = m_Direction * m_LaunchSpeed;
	m_Ball->SetPocketHandler([this]() { Destroy(); });

	render->LoadModel(
		"assets/model/GolfBall/golf_ball.obj",
		"assets/model/GolfBall");
	render->SetTint(Color(0.64f, 0.38f, 0.96f, 0.75f));
	const float scale = m_Status.radius /
		(std::max)(0.001f, render->GetModelBaseRadius());
	m_Ball->SetScale(Vector3(scale));
}

void PierceMinion::FixedUpdate()
{
	if (m_Ball == nullptr) return;
	BallPhysicsRules::EnemyFriction(
		m_Ball->GetMutableVelocity(),
		m_Ball->GetStatus().friction);
	if (m_Ball->GetPosition().y < -100.0f || m_Ball->IsStopped())
		Destroy();
}

void PierceMinion::Draw()
{
	if (m_Ball == nullptr) return;
	auto* render = GetGameObject()->GetComponent<BallRenderComponent>();
	if (render == nullptr) return;
	GameRuntime::MainCamera().SetCamera();
	render->BeginDraw();
	const Matrix world = Matrix::CreateScale(m_Ball->GetScale()) *
		Matrix::CreateFromQuaternion(m_Ball->GetMutableRollingRotation()) *
		Matrix::CreateTranslation(m_Ball->GetPosition());
	render->DrawMesh(world);
}

void PierceMinion::OnDestroy()
{
	if (m_Ball != nullptr) m_Ball->SetPocketHandler({});
}

void PierceMinion::Destroy()
{
	if (m_Ball != nullptr)
	{
		m_Ball->GetMutableVelocity() = Vector3::Zero;
		m_Ball->GetMutableAcceleration() = Vector3::Zero;
	}
	if (GetGameObject() != nullptr) GetGameObject()->Destroy();
}
