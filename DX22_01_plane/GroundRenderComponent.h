#pragma once

#include "Component.h"
#include "IndexBuffer.h"
#include "Material.h"
#include "Shader.h"
#include "Texture.h"
#include "VertexBuffer.h"

#include <memory>
#include <vector>

// 単位地面メッシュと、描画に必要なGPUリソースを所有
// TransformComponentのワールド行列を使用してフィールドへ描画
class GroundRenderComponent final : public Component
{
public:
    // 単位平面の頂点・インデックス、シェーダー、テクスチャ、マテリアルを生成
    void Awake() override;

    // カメラとTransformが有効な場合に、保持しているGPUリソースを使って地面を描画
    void Draw() override;

    // 生成済みのローカル頂点一覧を返す
    const std::vector<VERTEX_3D>& GetLocalVertices() const
    {
        return m_Vertices;
    }

private:
    std::vector<VERTEX_3D> m_Vertices;       // 地面メッシュのローカル頂点
    std::vector<unsigned int> m_Indices;     // 描画に使用する頂点インデックス

    VertexBuffer<VERTEX_3D> m_VertexBuffer;  // 地面頂点を保持するGPU頂点バッファ
    IndexBuffer m_IndexBuffer;               // 地面インデックスを保持するGPUインデックスバッファ
    Shader m_Shader;                         // 地面描画用シェーダー
    Texture m_Texture;                       // ビリヤードクロスのテクスチャ
    std::unique_ptr<Material> m_Material;    // 地面描画に使用するマテリアル
};
