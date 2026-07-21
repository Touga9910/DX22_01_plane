#include "Pocket.h"

#include "GameObject.h"
#include "SphereColliderComponent.h"

using namespace DirectX::SimpleMath;

void Pocket::Init()
{
}

void Pocket::Update()
{
}

void Pocket::Draw(Camera* cam)
{
    // ポケットの見た目は TableFrame 側の黒い円で描画しているため、
    // Pocket クラスでは描画しない
}

void Pocket::Uninit()
{
}

void Pocket::SetPosition(const Vector3& position)
{
    // Object::GetPosition() を使う可能性もあるため、両方に入れておく
    m_Position = position;
    m_Transform.position = position;
}

void Pocket::SetRadius(float radius)
{
    m_Radius = radius;

	if (GameObject* owner = GetGameObject())
	{
		if (SphereColliderComponent* collider =
			owner->GetComponent<SphereColliderComponent>())
		{
			collider->SetRadius(radius);
		}
	}
}

Collision::Sphere Pocket::GetSphere() const
{
    Collision::Sphere sphere;
    sphere.center = m_Transform.position;
    sphere.radius = m_Radius;

    return sphere;
}
