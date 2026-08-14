#pragma once

#include "Component.h"
#include "IndexBuffer.h"
#include "Material.h"
#include "Shader.h"
#include "VertexBuffer.h"

#include <memory>
#include <vector>

// Owns the frame mesh and GPU resources. Current continuous-rail geometry
// contains no embedded pocket centers; Pocket is an independent component.
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
