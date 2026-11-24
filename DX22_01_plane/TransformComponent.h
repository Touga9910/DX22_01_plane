#pragma once

#include <SimpleMath.h>

#include "Component.h"

class TransformComponent : public Component
{
public:
    /// <summary>
    /// 現在の位置を返す。
    /// </summary>
    const DirectX::SimpleMath::Vector3& GetPosition() const
    {
        return m_Position;
    }

    /// <summary>
    /// 位置を指定された座標に変更する。
    /// </summary>
    void SetPosition(
        const DirectX::SimpleMath::Vector3& position
    )
    {
        m_Position = position;
    }

    /// <summary>
    /// 現在の回転をクォータニオンで返す。
    /// </summary>
    const DirectX::SimpleMath::Quaternion& GetRotation() const
    {
        return m_Rotation;
    }

    /// <summary>
    /// 回転を指定されたクォータニオンに変更する。
    /// </summary>
    void SetRotation(
        const DirectX::SimpleMath::Quaternion& rotation
    )
    {
        m_Rotation = rotation;
    }

    /// <summary>
    /// 各軸の現在の拡縮率を返す。
    /// </summary>
    const DirectX::SimpleMath::Vector3& GetScale() const
    {
        return m_Scale;
    }

    /// <summary>
    /// 各軸の拡縮率を設定する。
    /// </summary>
    void SetScale(
        const DirectX::SimpleMath::Vector3& scale
    )
    {
        m_Scale = scale;
    }

    // 現在位置へ移動量を加える
    /// <summary>
    /// 指定された移動量を現在の位置に加える。
    /// </summary>
    void Translate(
        const DirectX::SimpleMath::Vector3& movement
    )
    {
        m_Position += movement;
    }

    // 描画に使用するワールド行列を生成する
    /// <summary>
    /// 拡縮、回転、平行移動を組み合わせてワールド変換行列を求める。
    /// </summary>
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