#include "Pocket.h"

#include "GameObject.h"
#include "SphereColliderComponent.h"
#include "TransformComponent.h"

#include <algorithm>

using namespace DirectX::SimpleMath;

Pocket::Pocket(float initialRadius)
    : m_InitialRadius((std::max)(0.0f, initialRadius))
{
}

void Pocket::Awake()
{
    GameObject* owner = GetGameObject();
    if (owner == nullptr)
    {
        return;
    }

    m_ColliderComponent = owner->GetComponent<SphereColliderComponent>();
    if (m_ColliderComponent == nullptr)
    {
        owner->Destroy();
        return;
    }

    m_ColliderComponent->SetRadius(m_InitialRadius);
    m_ColliderComponent->SetTrigger(true);
}

void Pocket::SetPosition(const Vector3& position)
{
    if (TransformComponent* transform = GetTransform())
    {
        transform->SetPosition(position);
    }
}

void Pocket::SetRadius(float radius)
{
    m_InitialRadius = (std::max)(0.0f, radius);

    if (m_ColliderComponent != nullptr)
    {
        m_ColliderComponent->SetRadius(m_InitialRadius);
    }
}

Collision::Sphere Pocket::GetSphere() const
{
    if (m_ColliderComponent != nullptr)
    {
        return m_ColliderComponent->GetSphere();
    }

    const TransformComponent* transform = GetTransform();
    return {
        transform != nullptr ? transform->GetPosition() : Vector3::Zero,
        m_InitialRadius
    };
}
