#pragma once

#include "Collision.h"
#include "Component.h"

class SphereColliderComponent;

// Pocket gameplay marker. Its trigger shape is composed with a
// SphereColliderComponent on the same GameObject.
class Pocket final : public Component
{
public:
    explicit Pocket(float initialRadius = 2.0f);

    void Awake() override;

    void SetPosition(const DirectX::SimpleMath::Vector3& position);
    void SetRadius(float radius);

    Collision::Sphere GetSphere() const;

private:
    float m_InitialRadius = 2.0f;
    SphereColliderComponent* m_ColliderComponent = nullptr;
};
