#pragma once

#include <algorithm>

#include "Collision.h"
#include "Component.h"
#include "TransformComponent.h"

// GameObjectへ球形の衝突判定情報を付与するコンポーネント
// 中心座標は所有GameObjectのTransformComponentから取得
class SphereColliderComponent final : public Component
{
public:
    // 指定半径とTrigger設定で生成
    // radiusが負の場合は0へ補正
    explicit SphereColliderComponent(float radius = 1.0f, bool isTrigger = false)
        : m_Radius((std::max)(0.0f, radius)),
        m_IsTrigger(isTrigger)
    {
    }

    void SetRadius(float radius) { m_Radius = (std::max)(0.0f, radius); } // 半径を0以上へ補正して設定
    float GetRadius() const { return m_Radius; } // 現在の球半径を返す

    void SetTrigger(bool isTrigger) { m_IsTrigger = isTrigger; } // Triggerとして扱うかを設定
    bool IsTrigger() const { return m_IsTrigger; } // Trigger設定ならtrueを返す

    // 現在のTransform位置と半径からCollision::Sphereを生成して返す
    // Transformを取得できない場合は原点を中心として返す
    Collision::Sphere GetSphere() const
    {
        const TransformComponent* transform = GetTransform();
        const auto center = transform != nullptr
            ? transform->GetPosition()
            : DirectX::SimpleMath::Vector3::Zero;

        return { center, m_Radius };
    }

private:
    float m_Radius = 1.0f;    // 球形コライダーの半径
    bool m_IsTrigger = false; // trueの場合、接触検出用のTriggerとして扱う
};
