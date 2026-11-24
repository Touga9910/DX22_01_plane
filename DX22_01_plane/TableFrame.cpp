#include "TableFrame.h"

#include "GameObject.h"
#include "TableFrameCollisionComponent.h"
#include "TableFrameRenderComponent.h"
#include "TransformComponent.h"

#include <algorithm>

using namespace DirectX::SimpleMath;

/// <summary>
/// 台枠の描画と衝突のコンポーネントを取得し、台枠を盤面の高さに配置する。
/// </summary>
void TableFrame::Awake()
{
    GameObject* owner = GetGameObject();
    if (owner == nullptr)
    {
        return;
    }

    m_CollisionComponent =
        owner->GetComponent<TableFrameCollisionComponent>();
    m_RenderComponent =
        owner->GetComponent<TableFrameRenderComponent>();

    if (m_CollisionComponent == nullptr || m_RenderComponent == nullptr)
    {
        owner->Destroy();
        return;
    }

    if (TransformComponent* transform = GetTransform())
    {
        transform->SetPosition(Vector3(0.0f, TableConfig::FIELD_HEIGHT, 0.0f));
    }
}

/// <summary>
/// 台枠の衝突線分をワールド座標に変換して返す。
/// </summary>
std::vector<Collision::Segment> TableFrame::GetWalls() const
{
    std::vector<Collision::Segment> result;
    if (m_CollisionComponent == nullptr)
    {
        return result;
    }

    const std::vector<Collision::Segment>& localWalls =
        m_CollisionComponent->GetLocalWalls();
    result.reserve(localWalls.size());

    const TransformComponent* transform = GetTransform();
    const Matrix worldMatrix = transform != nullptr
        ? transform->GetWorldMatrix()
        : Matrix::Identity;

    for (const Collision::Segment& wall : localWalls)
    {
        result.push_back({
            Vector3::Transform(wall.start, worldMatrix),
            Vector3::Transform(wall.end, worldMatrix)
        });
    }

    return result;
}

/// <summary>
/// ポケットの中心をワールド座標に変換し、当たり判定用の球を返す。
/// </summary>
std::vector<Collision::Sphere> TableFrame::GetPocketSpheres() const
{
    std::vector<Collision::Sphere> result;
    if (m_RenderComponent == nullptr)
    {
        return result;
    }

    const std::vector<Vector3>& localCenters =
        m_RenderComponent->GetLocalPocketCenters();
    result.reserve(localCenters.size());

    const TransformComponent* transform = GetTransform();
    const Matrix worldMatrix = transform != nullptr
        ? transform->GetWorldMatrix()
        : Matrix::Identity;
    const Vector3 scale = transform != nullptr
        ? transform->GetScale()
        : Vector3::One;
    const float maxScale =
        (std::max)(scale.x, (std::max)(scale.y, scale.z));

    for (const Vector3& center : localCenters)
    {
        result.push_back({
            Vector3::Transform(center, worldMatrix),
            TableConfig::POCKET_RADIUS * maxScale
        });
    }

    return result;
}
