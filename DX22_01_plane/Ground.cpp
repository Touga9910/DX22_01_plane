#include "Ground.h"

#include "GameObject.h"
#include "GroundRenderComponent.h"
#include "TransformComponent.h"

using namespace DirectX::SimpleMath;

void Ground::Awake()
{
    GameObject* owner = GetGameObject();
    if (owner == nullptr)
    {
        return;
    }

    m_RenderComponent = owner->GetComponent<GroundRenderComponent>();
    if (m_RenderComponent == nullptr)
    {
        owner->Destroy();
        return;
    }

    TransformComponent* transform = GetTransform();
    if (transform != nullptr)
    {
        transform->SetPosition(Vector3(0.0f, TableConfig::FIELD_HEIGHT, 0.0f));
        transform->SetScale(Vector3(
            TableConfig::GetFieldWidth(),
            1.0f,
            TableConfig::GetFieldDepth()));
    }
}

std::vector<VERTEX_3D> Ground::GetVertices()
{
    std::vector<VERTEX_3D> result;
    if (m_RenderComponent == nullptr)
    {
        return result;
    }

    const std::vector<VERTEX_3D>& localVertices =
        m_RenderComponent->GetLocalVertices();
    result.resize(localVertices.size());

    const TransformComponent* transform = GetTransform();
    const Matrix worldMatrix = transform != nullptr
        ? transform->GetWorldMatrix()
        : Matrix::Identity;

    for (std::size_t i = 0; i < localVertices.size(); ++i)
    {
        result[i].position =
            Vector3::Transform(localVertices[i].position, worldMatrix);
        result[i].normal =
            Vector3::Transform(localVertices[i].normal, worldMatrix);
        result[i].color = localVertices[i].color;
        result[i].uv = localVertices[i].uv;
    }

    return result;
}
