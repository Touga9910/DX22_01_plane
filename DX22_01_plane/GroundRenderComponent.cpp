#include "GroundRenderComponent.h"

#include "Camera.h"
#include "GameRuntime.h"
#include "Renderer.h"
#include "TableVisualMesh.h"
#include "TransformComponent.h"

#include <cassert>

using namespace DirectX::SimpleMath;

void GroundRenderComponent::Awake()
{
    // Ground keeps its existing field-size transform. Normalize the render-only
    // cut-out mesh here so neither the field bounds nor gameplay coordinates move.
    const auto felt = TableVisualMesh::BuildFelt();
    m_Vertices.clear();
    m_Vertices.reserve(felt.size());
    for (const auto& source : felt)
    {
        VERTEX_3D vertex{};
        const float x = source.position.x / TableConfig::GetFieldWidth();
        const float z = source.position.z / TableConfig::GetFieldDepth();
        vertex.position = Vector3(x, 0.0f, z);
        vertex.uv = Vector2(x + 0.5f, 0.5f - z);
        vertex.color = Color(1.0f, 1.0f, 1.0f, 1.0f);
        vertex.normal = Vector3::UnitY;
        m_Vertices.push_back(vertex);
    }

    m_VertexBuffer.Create(m_Vertices);

    m_Indices.resize(m_Vertices.size());
    for (std::size_t index = 0; index < m_Indices.size(); ++index)
    {
        m_Indices[index] = static_cast<unsigned int>(index);
    }
    m_IndexBuffer.Create(m_Indices);

    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    const bool textureLoaded =
        m_Texture.Load("assets/texture/billiard_felt.png");
    assert(textureLoaded);

    m_Material = std::make_unique<Material>();
    MATERIAL material{};
    // Dark billiard green (#176C4B). Applying it as a material tint keeps the
    // existing felt texture and its weave while leaving balls and rails alone.
    material.Diffuse = Color(
        23.0f / 255.0f,
        108.0f / 255.0f,
        75.0f / 255.0f,
        1.0f);
    material.TextureEnable = true;
    m_Material->Create(material);
}

void GroundRenderComponent::Draw()
{
    Camera* camera = &GameRuntime::MainCamera();
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
    m_Texture.SetGPU();
    m_Material->SetGPU();

    deviceContext->DrawIndexed(
        static_cast<UINT>(m_Indices.size()),
        0,
        0);
}
