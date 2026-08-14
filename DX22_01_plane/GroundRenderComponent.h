#pragma once

#include "Component.h"
#include "IndexBuffer.h"
#include "Material.h"
#include "Shader.h"
#include "Texture.h"
#include "VertexBuffer.h"

#include <memory>
#include <vector>

// Owns the unit ground mesh and all GPU resources required to draw it.
class GroundRenderComponent final : public Component
{
public:
    void Awake() override;
    void Draw() override;

    const std::vector<VERTEX_3D>& GetLocalVertices() const
    {
        return m_Vertices;
    }

private:
    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;

    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer m_IndexBuffer;
    Shader m_Shader;
    Texture m_Texture;
    std::unique_ptr<Material> m_Material;
};
