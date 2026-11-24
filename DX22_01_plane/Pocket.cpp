#include "Pocket.h"

#include "GameObject.h"
#include "SphereColliderComponent.h"
#include "TransformComponent.h"

#include <algorithm>

using namespace DirectX::SimpleMath;

/// <summary>
/// ポケットの初期半径を0以上に補正して保持する。
/// </summary>
Pocket::Pocket(float initialRadius)
    : m_InitialRadius((std::max)(0.0f, initialRadius))
{
}

/// <summary>
/// ポケットの当たり判定とトリガーをゲームオブジェクトに設定する。
/// </summary>
void Pocket::Awake()
{
    GameObject* owner = GetGameObject();
    if (owner == nullptr)
    {
        return;
    }

    m_ColliderComponent = owner->GetComponent<SphereColliderComponent>();
    if (m_ColliderComponent == nullptr)
    {
        owner->Destroy();
        return;
    }

    m_ColliderComponent->SetRadius(m_InitialRadius);
    m_ColliderComponent->SetTrigger(true);
}

/// <summary>
/// ポケットのワールド座標を更新する。
/// </summary>
void Pocket::SetPosition(const Vector3& position)
{
    if (TransformComponent* transform = GetTransform())
    {
        transform->SetPosition(position);
    }
}

/// <summary>
/// ポケットの半径を0以上に補正し、生成済みの当たり判定にも反映する。
/// </summary>
void Pocket::SetRadius(float radius)
{
    m_InitialRadius = (std::max)(0.0f, radius);

    if (m_ColliderComponent != nullptr)
    {
        m_ColliderComponent->SetRadius(m_InitialRadius);
    }
}

/// <summary>
/// 現在の位置と半径からポケットの球形当たり判定を返す。
/// </summary>
Collision::Sphere Pocket::GetSphere() const
{
    if (m_ColliderComponent != nullptr)
    {
        return m_ColliderComponent->GetSphere();
    }

    const TransformComponent* transform = GetTransform();
    return {
        transform != nullptr ? transform->GetPosition() : Vector3::Zero,
        m_InitialRadius
    };
}
