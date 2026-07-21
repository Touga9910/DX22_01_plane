#include "Object.h"
#include "Game.h"
#include "GameObject.h"
#include "TransformComponent.h"

using namespace DirectX::SimpleMath;

void Object::Awake()
{
	if (m_IsInitialized)
	{
		return;
	}

	Init();
	m_IsInitialized = true;
	SyncTransformComponent();
	SynchronizeComponents();
}

void Object::LateUpdate()
{
	SyncTransformComponent();
	SynchronizeComponents();
}

void Object::Draw()
{
	Draw(Game::GetCamera());
}

void Object::OnDestroy()
{
	if (!m_IsInitialized)
	{
		return;
	}

	Uninit();
	m_IsInitialized = false;
}

void Object::Destroy()
{
	m_IsDead = true;

	if (GameObject* owner = GetGameObject())
	{
		owner->Destroy();
	}
}

void Object::SyncTransformComponent()
{
	TransformComponent* transform = Component::GetTransform();
	if (transform == nullptr)
	{
		return;
	}

	transform->SetPosition(m_Transform.position);
	transform->SetRotation(
		DirectX::SimpleMath::Quaternion::CreateFromYawPitchRoll(
			m_Transform.rotation.y,
			m_Transform.rotation.x,
			m_Transform.rotation.z));
	transform->SetScale(m_Transform.scale);
}
