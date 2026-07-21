#include "BallRenderComponent.h"

#include "Renderer.h"

#include <algorithm>

using namespace DirectX::SimpleMath;

void BallRenderComponent::LoadModel(
    const char* modelFilePath,
    const char* textureDirectory)
{
    StaticMesh staticMesh;
    staticMesh.Load(modelFilePath, textureDirectory);

    m_MeshRenderer.Init(staticMesh);
    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");
    m_Subsets = staticMesh.GetSubsets();
    m_Textures = staticMesh.GetTextures();
    m_Materials.clear();

    for (const MATERIAL& materialData : staticMesh.GetMaterials())
    {
        auto material = std::make_unique<Material>();
        material->Create(materialData);
        m_Materials.push_back(std::move(material));
    }

    float maxDistance = 0.0f;
    for (const auto& vertex : staticMesh.GetVertices())
    {
        const Vector3 position(
            vertex.position.x,
            vertex.position.y,
            vertex.position.z);
        maxDistance = (std::max)(maxDistance, position.Length());
    }

    m_ModelBaseRadius = maxDistance;
}

void BallRenderComponent::BeginDraw()
{
    m_Shader.SetGPU();
    m_MeshRenderer.BeforeDraw();
}

void BallRenderComponent::DrawMesh(const Matrix& worldMatrix)
{
    Renderer::SetWorldMatrix(const_cast<Matrix*>(&worldMatrix));

    for (const SUBSET& subset : m_Subsets)
    {
        const UINT materialIndex = subset.MaterialIdx;
        if (materialIndex >= m_Materials.size())
        {
            continue;
        }

        m_Materials[materialIndex]->SetGPU();

        if (m_Materials[materialIndex]->isTextureEnable() &&
            materialIndex < m_Textures.size())
        {
            m_Textures[materialIndex]->SetGPU();
        }

        m_MeshRenderer.DrawSubset(
            subset.IndexNum,
            subset.IndexBase,
            subset.VertexBase);
    }
}
