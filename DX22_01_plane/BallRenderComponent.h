#pragma once

#include "Component.h"
#include "Material.h"
#include "MeshRenderer.h"
#include "Shader.h"
#include "StaticMesh.h"
#include "Texture.h"

#include <memory>
#include <vector>

// Caromino: FBX (cue + balls 1-15) direct renderer.
// Uses only Billiard Balls.FBX and its JPG/JPEG diffuse textures.
// Existing PlayerBall/EnemyBall code and physical radii are unchanged.
class BallRenderComponent final : public Component
{
public:
    ~BallRenderComponent() override;
    void OnDestroy() override;

    void LoadModel(const char* modelFilePath, const char* textureDirectory);
    void SetTint(const DirectX::SimpleMath::Color& color);
    void SetSpecularStrength(float strength);
    void SetShininess(float shininess);
    void SetEnvironmentStrength(float strength);
    void SetDefeatVisualProgress(float progress);
    void SetSpecularEnabled(bool enabled) { m_SpecularEnabled = enabled; }
    void SetEnvironmentReflectionEnabled(bool enabled) { m_EnvironmentEnabled = enabled; }
    void BeginDraw();
    void DrawMesh(const DirectX::SimpleMath::Matrix& worldMatrix);

    float GetModelBaseRadius() const { return m_ModelBaseRadius; }
    int GetBilliardNumber() const { return m_BilliardNumber; } // 0: cue, 1-15: enemies

private:
    bool TryLoadBilliardFBX(const char* path, int ballNumber);
    void LoadOriginalModel(const char* modelFilePath, const char* textureDirectory);
    void ReleaseBilliardNumber();

    Shader m_Shader;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_ReflectionBuffer;
    bool m_ReflectionEnabled = false;
    bool m_SpecularEnabled = true;
    bool m_EnvironmentEnabled = true;
    float m_SpecularStrength = 0.55f;
    float m_Shininess = 86.0f;
    float m_EnvironmentStrength = 0.18f;
    float m_DefeatVisualProgress = 0.0f;
    bool m_RenderReady = false;
    bool CreateReflectionBuffer();
    void BindReflectionBuffer() const;
    MeshRenderer m_MeshRenderer;
    std::vector<std::unique_ptr<Material>> m_Materials;
    std::vector<SUBSET> m_Subsets;
    std::vector<std::unique_ptr<Texture>> m_Textures;
    float m_ModelBaseRadius = 1.0f;
    int m_BilliardNumber = -1;
    bool m_UsesBilliardModel = false;
    bool m_MissingBilliardTexture = false;
};
