#pragma once

#include "Component.h"
#include "SimpleMath.h"

class BallPhysicsComponent final : public Component
{
public:
    void Synchronize(
        const DirectX::SimpleMath::Vector3& velocity,
        const DirectX::SimpleMath::Vector3& acceleration,
        float mass,
        float restitution,
        float friction)
    {
        m_Velocity = velocity;
        m_Acceleration = acceleration;
        m_Mass = mass;
        m_Restitution = restitution;
        m_Friction = friction;
    }

    const DirectX::SimpleMath::Vector3& GetVelocity() const { return m_Velocity; }
    const DirectX::SimpleMath::Vector3& GetAcceleration() const { return m_Acceleration; }
    DirectX::SimpleMath::Vector3& Velocity() { return m_Velocity; }
    DirectX::SimpleMath::Vector3& Acceleration() { return m_Acceleration; }
    float GetMass() const { return m_Mass; }
    float GetRestitution() const { return m_Restitution; }
    float GetFriction() const { return m_Friction; }
    float& Mass() { return m_Mass; }
    float& Restitution() { return m_Restitution; }
    float& Friction() { return m_Friction; }

    float GetRadius() const { return m_Radius; }
    void SetRadius(float radius) { m_Radius = radius; }
    float& Radius() { return m_Radius; }

    DirectX::SimpleMath::Vector3& InitialPosition() { return m_InitialPosition; }
    DirectX::SimpleMath::Vector3& OldPosition() { return m_OldPosition; }
    DirectX::SimpleMath::Quaternion& RollingRotation() { return m_RollingRotation; }
    const DirectX::SimpleMath::Quaternion& GetRollingRotation() const { return m_RollingRotation; }

    bool IsStopped(float speedSquaredThreshold = 0.03f) const
    {
        return m_Velocity.LengthSquared() < speedSquaredThreshold;
    }

private:
    DirectX::SimpleMath::Vector3 m_Velocity = DirectX::SimpleMath::Vector3::Zero;
    DirectX::SimpleMath::Vector3 m_Acceleration = DirectX::SimpleMath::Vector3::Zero;
    float m_Mass = 1.0f;
    float m_Restitution = 0.8f;
    float m_Friction = 0.02f;
    float m_Radius = 2.4f;
    DirectX::SimpleMath::Vector3 m_InitialPosition = DirectX::SimpleMath::Vector3::Zero;
    DirectX::SimpleMath::Vector3 m_OldPosition = DirectX::SimpleMath::Vector3::Zero;
    DirectX::SimpleMath::Quaternion m_RollingRotation =
        DirectX::SimpleMath::Quaternion::Identity;
};
