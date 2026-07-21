#pragma once

#include <algorithm>

#include "Collision.h"
#include "Component.h"
#include "TransformComponent.h"

class SphereColliderComponent final : public Component
{
public:
    explicit SphereColliderComponent(float radius = 1.0f, bool isTrigger = false)
        : m_Radius((std::max)(0.0f, radius)),
          m_IsTrigger(isTrigger)
    {
    }

    void SetRadius(float radius) { m_Radius = (std::max)(0.0f, radius); }
    float GetRadius() const { return m_Radius; }

    void SetTrigger(bool isTrigger) { m_IsTrigger = isTrigger; }
    bool IsTrigger() const { return m_IsTrigger; }

    Collision::Sphere GetSphere() const
    {
        const TransformComponent* transform = GetTransform();
        const auto center = transform != nullptr
            ? transform->GetPosition()
            : DirectX::SimpleMath::Vector3::Zero;

        return { center, m_Radius };
    }

private:
    float m_Radius = 1.0f;
    bool m_IsTrigger = false;
};
