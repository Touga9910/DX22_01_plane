#pragma once

#include "Component.h"
#include "IndexBuffer.h"
#include "Material.h"
#include "Shader.h"
#include "VertexBuffer.h"

#include <memory>
#include <vector>

// 台の外枠・クッション・ポケット内壁の描画メッシュとGPUリソースを所有する。
// 描画形状はTableVisualMesh、反射壁と入球判定は従来の独立コンポーネントで管理。
class TableFrameRenderComponent final : public Component
{
public:
    void Awake() override;
    void Draw() override;
    /// <summary>
    /// チャージ表示を描画するかどうかを設定する。
    /// </summary>
    void SetChargeIndicatorsEnabled(bool enabled) { m_ShowChargeIndicators=enabled; }

    /// <summary>
    /// ローカル座標で保持しているポケットの中心を返す。
    /// </summary>
    const std::vector<DirectX::SimpleMath::Vector3>&
        GetLocalPocketCenters() const
    {
        return m_PocketCenters;
    }

private:
    bool m_ShowChargeIndicators=true;
    void BuildMesh();
	void BuildChargeIndicators();
	void AddChargeQuad(float xMin, float zMin, float xMax, float zMax, float y);
    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;
	std::vector<VERTEX_3D> m_ChargeVertices;
	std::vector<unsigned int> m_ChargeIndices;
    std::vector<DirectX::SimpleMath::Vector3> m_PocketCenters;

    VertexBuffer<VERTEX_3D> m_VertexBuffer;
    IndexBuffer m_IndexBuffer;
	VertexBuffer<VERTEX_3D> m_ChargeVertexBuffer;
	IndexBuffer m_ChargeIndexBuffer;
    Shader m_Shader;
    std::unique_ptr<Material> m_Material;
	std::unique_ptr<Material> m_ChargeMaterial;
};
