#pragma once

#include <SimpleMath.h>

#include "Component.h"

class TransformComponent;

class RigidbodyComponent : public Component
{
public:
    void Start() override;
    void FixedUpdate() override;

    const DirectX::SimpleMath::Vector3& GetVelocity() const
    {
        return m_Velocity;
    }

    void SetVelocity(
        const DirectX::SimpleMath::Vector3& velocity
    )
    {
        m_Velocity = velocity;
        m_IsStopped = false;
        m_StopCount = 0;
    }

    void AddVelocity(
        const DirectX::SimpleMath::Vector3& velocity
    )
    {
        m_Velocity += velocity;
        m_IsStopped = false;
        m_StopCount = 0;
    }

    void Stop()
    {
        m_Velocity = DirectX::SimpleMath::Vector3::Zero;
        m_IsStopped = true;
        m_StopCount = 0;
    }

    void SetFriction(float friction)
    {
        m_Friction = friction;
    }

    float GetFriction() const
    {
        return m_Friction;
    }

    bool IsStopped() const
    {
        return m_IsStopped;
    }

private:
    void ApplyFriction();
    void UpdateStopState();

private:
    TransformComponent* m_Transform = nullptr;

    DirectX::SimpleMath::Vector3 m_Velocity =
        DirectX::SimpleMath::Vector3::Zero;

    float m_Friction = 0.01f;

    float m_StopSpeedSquared = 0.03f;

    int m_StopCount = 0;
    int m_RequiredStopFrames = 10;

    bool m_IsStopped = true;
};
