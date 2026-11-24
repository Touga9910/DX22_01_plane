#pragma once

#include "Component.h"
#include "SimpleMath.h"

class BallPhysicsComponent final : public Component
{
public:
    /// <summary>
    /// 速度、加速度、質量、反発係数、摩擦係数をまとめて物理状態に設定する。
    /// </summary>
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

    /// <summary>
    /// ボールの現在速度を読み取り専用の参照で返す。
    /// </summary>
    const DirectX::SimpleMath::Vector3& GetVelocity() const { return m_Velocity; }
    /// <summary>
    /// ボールの現在加速度を読み取り専用の参照で返す。
    /// </summary>
    const DirectX::SimpleMath::Vector3& GetAcceleration() const { return m_Acceleration; }
    /// <summary>
    /// ボールの現在速度を変更できる参照を返す。
    /// </summary>
    DirectX::SimpleMath::Vector3& Velocity() { return m_Velocity; }
    /// <summary>
    /// ボールの現在加速度を変更できる参照を返す。
    /// </summary>
    DirectX::SimpleMath::Vector3& Acceleration() { return m_Acceleration; }
    /// <summary>
    /// ボールの質量を返す。
    /// </summary>
    float GetMass() const { return m_Mass; }
    /// <summary>
    /// ボールの反発係数を返す。
    /// </summary>
    float GetRestitution() const { return m_Restitution; }
    /// <summary>
    /// ボールの摩擦係数を返す。
    /// </summary>
    float GetFriction() const { return m_Friction; }
    /// <summary>
    /// ボールの質量を変更できる参照を返す。
    /// </summary>
    float& Mass() { return m_Mass; }
    /// <summary>
    /// ボールの反発係数を変更できる参照を返す。
    /// </summary>
    float& Restitution() { return m_Restitution; }
    /// <summary>
    /// ボールの摩擦係数を変更できる参照を返す。
    /// </summary>
    float& Friction() { return m_Friction; }

    /// <summary>
    /// 物理計算に使うボールの半径を返す。
    /// </summary>
    float GetRadius() const { return m_Radius; }
    /// <summary>
    /// 物理計算に使うボールの半径を設定する。
    /// </summary>
    void SetRadius(float radius) { m_Radius = radius; }
    /// <summary>
    /// 物理計算に使うボールの半径を変更できる参照を返す。
    /// </summary>
    float& Radius() { return m_Radius; }

    /// <summary>
    /// リセット先となるボールの初期位置を変更できる参照を返す。
    /// </summary>
    DirectX::SimpleMath::Vector3& InitialPosition() { return m_InitialPosition; }
    /// <summary>
    /// 物理ステップの移動前位置を変更できる参照を返す。
    /// </summary>
    DirectX::SimpleMath::Vector3& OldPosition() { return m_OldPosition; }
    /// <summary>
    /// ボールの転がり回転を変更できる参照を返す。
    /// </summary>
    DirectX::SimpleMath::Quaternion& RollingRotation() { return m_RollingRotation; }
    /// <summary>
    /// ボールの転がり回転を読み取り専用の参照で返す。
    /// </summary>
    const DirectX::SimpleMath::Quaternion& GetRollingRotation() const { return m_RollingRotation; }

    /// <summary>
    /// 速度の二乗が所定のしきい値を下回るかを判定する。
    /// </summary>
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
