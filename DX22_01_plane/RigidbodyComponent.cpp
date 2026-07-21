#include "RigidbodyComponent.h"

#include <algorithm>

#include "TransformComponent.h"

// 使用するTransformComponentを最初に取得する
void RigidbodyComponent::Start()
{
    m_Transform = GetTransform();
}

// 速度に応じてGameObjectを移動させる
void RigidbodyComponent::FixedUpdate()
{
    if (m_Transform == nullptr)
    {
        return;
    }

    if (m_IsStopped)
    {
        return;
    }

    m_Transform->Translate(m_Velocity);

    ApplyFriction();
    UpdateStopState();
}

// 速度の大きさを摩擦値分だけ減少させる
void RigidbodyComponent::ApplyFriction()
{
    const float speed = m_Velocity.Length();

    if (speed <= 0.0f)
    {
        m_Velocity = DirectX::SimpleMath::Vector3::Zero;
        return;
    }

    const float nextSpeed =
        (std::max)(0.0f, speed - m_Friction);

    if (nextSpeed <= 0.0f)
    {
        m_Velocity = DirectX::SimpleMath::Vector3::Zero;
        return;
    }

    m_Velocity.Normalize();
    m_Velocity *= nextSpeed;
}

// 一定時間低速状態が続いた場合に停止扱いにする
void RigidbodyComponent::UpdateStopState()
{
    if (m_Velocity.LengthSquared() < m_StopSpeedSquared)
    {
        ++m_StopCount;
    }
    else
    {
        m_StopCount = 0;
    }

    if (m_StopCount < m_RequiredStopFrames)
    {
        return;
    }

    m_Velocity = DirectX::SimpleMath::Vector3::Zero;
    m_IsStopped = true;
    m_StopCount = 0;
}
