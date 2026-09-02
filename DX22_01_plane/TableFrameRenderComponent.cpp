#include "TableFrameRenderComponent.h"

#include "Camera.h"
#include "Game.h"
#include "Renderer.h"
#include "TableConfig.h"
#include "TransformComponent.h"

#include <cmath>

using namespace DirectX::SimpleMath;

void TableFrameRenderComponent::Awake()
{
    BuildMesh();

    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    m_Material = std::make_unique<Material>();
    MATERIAL material{};
    material.Diffuse = Color(1.0f, 1.0f, 1.0f, 1.0f);
    material.TextureEnable = false;
    m_Material->Create(material);
}

void TableFrameRenderComponent::Draw()
{
    Camera* camera = Game::GetCamera();
    TransformComponent* transform = GetTransform();
    if (camera == nullptr || transform == nullptr || m_Material == nullptr)
    {
        return;
    }

    camera->SetCamera();

    Matrix worldMatrix = transform->GetWorldMatrix();
    Renderer::SetWorldMatrix(&worldMatrix);

    ID3D11DeviceContext* deviceContext = Renderer::GetDeviceContext();
    deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    m_Shader.SetGPU();
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();
    m_Material->SetGPU();

    deviceContext->DrawIndexed(
        static_cast<UINT>(m_Indices.size()),
        0,
        0);
}

void TableFrameRenderComponent::BuildMesh()
{
    m_Vertices.clear();
    m_Indices.clear();
    m_PocketCenters.clear();

    const float outerHalfWidth = TableConfig::TABLE_OUTER_WIDTH * 0.5f;
    const float outerHalfDepth = TableConfig::TABLE_OUTER_DEPTH * 0.5f;
    const float fieldHalfWidth = TableConfig::GetFieldWidth() * 0.5f;
    const float fieldHalfDepth = TableConfig::GetFieldDepth() * 0.5f;
    const float mouth = TableConfig::POCKET_MOUTH_HALF_WIDTH;
    const float y = TableConfig::RAIL_TOP_OFFSET;
    const Color railColor(0.45f, 0.22f, 0.08f, 1.0f);
    const Color pocketColor(0.0f, 0.0f, 0.0f, 1.0f);

    AddQuad(-fieldHalfWidth + mouth, fieldHalfDepth, -mouth, outerHalfDepth, y, railColor);
    AddQuad(mouth, fieldHalfDepth, fieldHalfWidth - mouth, outerHalfDepth, y, railColor);
    AddQuad(-fieldHalfWidth + mouth, -outerHalfDepth, -mouth, -fieldHalfDepth, y, railColor);
    AddQuad(mouth, -outerHalfDepth, fieldHalfWidth - mouth, -fieldHalfDepth, y, railColor);
    AddQuad(-outerHalfWidth, -fieldHalfDepth + mouth, -fieldHalfWidth, fieldHalfDepth - mouth, y, railColor);
    AddQuad(fieldHalfWidth, -fieldHalfDepth + mouth, outerHalfWidth, fieldHalfDepth - mouth, y, railColor);

    for (const Vector3& center : TableConfig::GetPocketCenters())
    {
        m_PocketCenters.push_back(center);
        AddPocketDisc(
            center,
            TableConfig::POCKET_RADIUS * 1.45f,
            y + 0.02f,
            pocketColor);
    }
}

void TableFrameRenderComponent::AddPocketDisc(
    const Vector3& center,
    float radius,
    float y,
    const Color& color)
{
    constexpr int segments = 24;
    constexpr float twoPi = 6.28318530717958647692f;
    for (int segment = 0; segment < segments; segment++)
    {
        const float angle0 = twoPi * static_cast<float>(segment) /
            static_cast<float>(segments);
        const float angle1 = twoPi * static_cast<float>(segment + 1) /
            static_cast<float>(segments);
        const unsigned int startIndex =
            static_cast<unsigned int>(m_Vertices.size());

        VERTEX_3D centerVertex{};
        VERTEX_3D edge0{};
        VERTEX_3D edge1{};
        centerVertex.position = Vector3(center.x, y, center.z);
        edge0.position = Vector3(
            center.x + std::cos(angle0) * radius,
            y,
            center.z + std::sin(angle0) * radius);
        edge1.position = Vector3(
            center.x + std::cos(angle1) * radius,
            y,
            center.z + std::sin(angle1) * radius);
        centerVertex.normal = edge0.normal = edge1.normal = Vector3::UnitY;
        centerVertex.color = edge0.color = edge1.color = color;
        centerVertex.uv = Vector2(0.5f, 0.5f);
        edge0.uv = Vector2(0.0f, 0.0f);
        edge1.uv = Vector2(1.0f, 0.0f);
        m_Vertices.push_back(centerVertex);
        m_Vertices.push_back(edge0);
        m_Vertices.push_back(edge1);
        m_Indices.push_back(startIndex);
        m_Indices.push_back(startIndex + 2);
        m_Indices.push_back(startIndex + 1);
    }
}

void TableFrameRenderComponent::AddQuad(
    float xMin,
    float zMin,
    float xMax,
    float zMax,
    float y,
    const Color& color)
{
    if (xMin >= xMax || zMin >= zMax)
    {
        return;
    }

    const unsigned int startIndex =
        static_cast<unsigned int>(m_Vertices.size());

    VERTEX_3D topLeft{};
    VERTEX_3D topRight{};
    VERTEX_3D bottomLeft{};
    VERTEX_3D bottomRight{};

    topLeft.position = Vector3(xMin, y, zMax);
    topRight.position = Vector3(xMax, y, zMax);
    bottomLeft.position = Vector3(xMin, y, zMin);
    bottomRight.position = Vector3(xMax, y, zMin);

    topLeft.uv = Vector2(0.0f, 0.0f);
    topRight.uv = Vector2(1.0f, 0.0f);
    bottomLeft.uv = Vector2(0.0f, 1.0f);
    bottomRight.uv = Vector2(1.0f, 1.0f);

    topLeft.color = topRight.color = bottomLeft.color = bottomRight.color = color;
    topLeft.normal = topRight.normal = bottomLeft.normal = bottomRight.normal = Vector3::UnitY;

    m_Vertices.push_back(topLeft);
    m_Vertices.push_back(topRight);
    m_Vertices.push_back(bottomLeft);
    m_Vertices.push_back(bottomRight);

    m_Indices.push_back(startIndex + 0);
    m_Indices.push_back(startIndex + 1);
    m_Indices.push_back(startIndex + 2);
    m_Indices.push_back(startIndex + 2);
    m_Indices.push_back(startIndex + 1);
    m_Indices.push_back(startIndex + 3);
}
