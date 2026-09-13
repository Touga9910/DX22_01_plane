#pragma once

#include <SimpleMath.h>

#include "Component.h"

class TransformComponent : public Component
{
public:
    const DirectX::SimpleMath::Vector3& GetPosition() const
    {
        return m_Position;
    }

    void SetPosition(
        const DirectX::SimpleMath::Vector3& position
    )
    {
        m_Position = position;
    }

    const DirectX::SimpleMath::Quaternion& GetRotation() const
    {
        return m_Rotation;
    }

    void SetRotation(
        const DirectX::SimpleMath::Quaternion& rotation
    )
    {
        m_Rotation = rotation;
    }

    const DirectX::SimpleMath::Vector3& GetScale() const
    {
        return m_Scale;
    }

    void SetScale(
        const DirectX::SimpleMath::Vector3& scale
    )
    {
        m_Scale = scale;
    }

    // 現在位置へ移動量を加える
    void Translate(
        const DirectX::SimpleMath::Vector3& movement
    )
    {
        m_Position += movement;
    }

    // 描画に使用するワールド行列を生成する
    DirectX::SimpleMath::Matrix GetWorldMatrix() const
    {
        using namespace DirectX::SimpleMath;

        return Matrix::CreateScale(m_Scale) *
            Matrix::CreateFromQuaternion(m_Rotation) *
            Matrix::CreateTranslation(m_Position);
    }

private:
    DirectX::SimpleMath::Vector3 m_Position =
        DirectX::SimpleMath::Vector3::Zero;

    DirectX::SimpleMath::Quaternion m_Rotation =
        DirectX::SimpleMath::Quaternion::Identity;

    DirectX::SimpleMath::Vector3 m_Scale =
        DirectX::SimpleMath::Vector3::One;
};