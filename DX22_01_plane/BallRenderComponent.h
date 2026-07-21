#pragma once

#include "Component.h"
#include "Material.h"
#include "MeshRenderer.h"
#include "Shader.h"
#include "StaticMesh.h"
#include "Texture.h"

#include <memory>
#include <vector>

// Ball model resources and mesh drawing are isolated from ball control/physics.
class BallRenderComponent final : public Component
{
public:
    void LoadModel(const char* modelFilePath, const char* textureDirectory);
    void BeginDraw();
    void DrawMesh(const DirectX::SimpleMath::Matrix& worldMatrix);

    float GetModelBaseRadius() const { return m_ModelBaseRadius; }

private:
    Shader m_Shader;
    MeshRenderer m_MeshRenderer;
    std::vector<std::unique_ptr<Material>> m_Materials;
    std::vector<SUBSET> m_Subsets;
    std::vector<std::unique_ptr<Texture>> m_Textures;
    float m_ModelBaseRadius = 1.0f;
};
