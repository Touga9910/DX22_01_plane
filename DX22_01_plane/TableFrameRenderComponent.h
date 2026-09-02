#pragma once

#include "Component.h"
#include "IndexBuffer.h"
#include "Material.h"
#include "Shader.h"
#include "VertexBuffer.h"

#include <memory>
#include <vector>

// 枠のメッシュとGPUリソースを所有する。現在の連続レール形状には
// ポケット中心を埋め込まず、Pocketを独立したコンポーネントとして扱う。
class TableFrameRenderComponent final : public Component
{
public:
    void Awake() override;
    void Draw() override;

    const std::vector<DirectX::SimpleMath::Vector3>&
        GetLocalPocketCenters() const
    {
        return m_PocketCenters;
    }

private:
    void BuildMesh();
    void AddQuad(
        float xMin,
        float zMin,
        float xMax,
        float zMax,
        float y,
        const DirectX::SimpleMath::Color& color);
    void AddPocketDisc(
        const DirectX::SimpleMath::Vector3& center,
        float radius,
        float y,
        const DirectX::SimpleMath::Color& color);

    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;
    std::vector<DirectX::SimpleMath::Vector3> m_PocketCenters;

    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer m_IndexBuffer;
    Shader m_Shader;
    std::unique_ptr<Material> m_Material;
};
