#include "GroundRenderComponent.h"

#include "Camera.h"
#include "Game.h"
#include "Renderer.h"
#include "TransformComponent.h"

#include <cassert>

using namespace DirectX::SimpleMath;

void GroundRenderComponent::Awake()
{
    constexpr int sizeX = 1;
    constexpr int sizeZ = 1;

    m_Vertices.resize(6 * sizeX * sizeZ);

    for (int z = 0; z < sizeZ; ++z)
    {
        for (int x = 0; x < sizeX; ++x)
        {
            const int n = z * sizeX * 6 + x * 6;

            m_Vertices[n + 0].position = Vector3(-0.5f + x - sizeX / 2, 0.0f, 0.5f - z + sizeZ / 2);
            m_Vertices[n + 1].position = Vector3(0.5f + x - sizeX / 2, 0.0f, 0.5f - z + sizeZ / 2);
            m_Vertices[n + 2].position = Vector3(-0.5f + x - sizeX / 2, 0.0f, -0.5f - z + sizeZ / 2);
            m_Vertices[n + 3].position = Vector3(-0.5f + x - sizeX / 2, 0.0f, -0.5f - z + sizeZ / 2);
            m_Vertices[n + 4].position = Vector3(0.5f + x - sizeX / 2, 0.0f, 0.5f - z + sizeZ / 2);
            m_Vertices[n + 5].position = Vector3(0.5f + x - sizeX / 2, 0.0f, -0.5f - z + sizeZ / 2);

            m_Vertices[n + 0].uv = Vector2(0.0f, 0.0f);
            m_Vertices[n + 1].uv = Vector2(1.0f, 0.0f);
            m_Vertices[n + 2].uv = Vector2(0.0f, 1.0f);
            m_Vertices[n + 3].uv = Vector2(0.0f, 1.0f);
            m_Vertices[n + 4].uv = Vector2(1.0f, 0.0f);
            m_Vertices[n + 5].uv = Vector2(1.0f, 1.0f);

            for (int vertexIndex = 0; vertexIndex < 6; ++vertexIndex)
            {
                VERTEX_3D& vertex = m_Vertices[n + vertexIndex];
                vertex.color = Color(1.0f, 1.0f, 1.0f, 1.0f);
                vertex.normal = Vector3::UnitY;
            }
        }
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
    material.Diffuse = Color(1.0f, 1.0f, 1.0f, 1.0f);
    material.TextureEnable = true;
    m_Material->Create(material);
}

void GroundRenderComponent::Draw()
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
    m_Texture.SetGPU();
    m_Material->SetGPU();

    deviceContext->DrawIndexed(
        static_cast<UINT>(m_Indices.size()),
        0,
        0);
}
