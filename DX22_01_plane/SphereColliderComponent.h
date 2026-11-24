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
    /// <summary>
    /// 半径を0以上に補正して球形コライダーを初期化する。
    /// </summary>
    explicit SphereColliderComponent(float radius = 1.0f, bool isTrigger = false)
        : m_Radius((std::max)(0.0f, radius)),
        m_IsTrigger(isTrigger)
    {
    }

    /// <summary>
    /// 球形コライダーの半径を0以上の値に更新する。
    /// </summary>
    void SetRadius(float radius) { m_Radius = (std::max)(0.0f, radius); } // 半径を0以上へ補正して設定
    /// <summary>
    /// 球形コライダーの現在の半径を返す。
    /// </summary>
    float GetRadius() const { return m_Radius; } // 現在の球半径を返す

    /// <summary>
    /// 球形コライダーをトリガーとして扱うかを設定する。
    /// </summary>
    void SetTrigger(bool isTrigger) { m_IsTrigger = isTrigger; } // Triggerとして扱うかを設定
    /// <summary>
    /// 球形コライダーがトリガーとして設定されているかを返す。
    /// </summary>
    bool IsTrigger() const { return m_IsTrigger; } // Trigger設定ならtrueを返す

    // 現在のTransform位置と半径からCollision::Sphereを生成して返す
    // Transformを取得できない場合は原点を中心として返す
    /// <summary>
    /// オブジェクトの位置を中心に球形の当たり判定を作り、位置を取得できない場合は原点を使う。
    /// </summary>
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
