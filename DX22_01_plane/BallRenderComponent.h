#pragma once

#include "Component.h"
#include "Material.h"
#include "MeshRenderer.h"
#include "Shader.h"
#include "StaticMesh.h"
#include "Texture.h"

#include <memory>
#include <vector>

// ボールの制御や物理処理から、モデルリソースとメッシュ描画を分離する。
class BallRenderComponent final : public Component
{
public:
    void LoadModel(const char* modelFilePath, const char* textureDirectory);
    void SetTint(const DirectX::SimpleMath::Color& color);
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
