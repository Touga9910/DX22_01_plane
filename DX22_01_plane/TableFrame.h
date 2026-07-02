#pragma once

#include <vector>
#include <memory>

#include "Object.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Camera.h"
#include "Shader.h"
#include "Material.h"
#include "Collision.h"

#include "TableConfig.h"

class TableFrame : public Object
{
private:
    DirectX::SimpleMath::Vector3 m_Position = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
    DirectX::SimpleMath::Vector3 m_Rotation = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);

    DirectX::SimpleMath::Vector3 m_Scale = DirectX::SimpleMath::Vector3(1.0f, 1.0f, 1.0f);

    std::vector<VERTEX_3D> m_Vertices;
    std::vector<unsigned int> m_Indices;

    IndexBuffer m_IndexBuffer;
    VertexBuffer<VERTEX_3D> m_VertexBuffer;

    std::unique_ptr<Material> m_Material;

    std::vector<Collision::Segment> m_Walls;
    std::vector<DirectX::SimpleMath::Vector3> m_PocketCenters;

private:
    /// <summary>
	/// レールとポケットのメッシュを構築する関数
    /// </summary>
    void BuildMesh();
    /// <summary>
	/// ボールが反射する壁を構築する関数
    /// </summary>
    void BuildWalls();

    /// <summary>
    /// 一枚の長方形を作る補助関数
    /// </summary>
    void AddQuad(
        float xMin,
        float zMin,
        float xMax,
        float zMax,
        float y,
        const DirectX::SimpleMath::Color& color);

    /// <summary>
	/// ポケットの円形のメッシュを作る補助関数
    /// </summary>
    void AddDisc(
        float centerX,
        float centerZ,
        float radius,
        int split,
        float y,
        const DirectX::SimpleMath::Color& color);

    /// <summary>
	/// m_wallsに壁の線分を追加する補助関数
    /// </summary>
    void AddWall(
        const DirectX::SimpleMath::Vector3& start,
        const DirectX::SimpleMath::Vector3& end);

public:
    virtual ~TableFrame() {}

    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void Uninit() override;

    std::vector<Collision::Segment> GetWalls() const;
    std::vector<Collision::Sphere> GetPocketSpheres() const;

	// コンフィグから必要情報を取得
    float GetOuterWidth() const { return TableConfig::TABLE_OUTER_WIDTH; }
    float GetOuterDepth() const { return TableConfig::TABLE_OUTER_DEPTH; }

    float GetFieldWidth() const { return TableConfig::GetFieldWidth(); }
    float GetFieldDepth() const { return TableConfig::GetFieldDepth(); }
    float GetFieldHeight() const { return TableConfig::FIELD_HEIGHT; }

    float GetRailWidth() const { return TableConfig::RAIL_WIDTH; }
    float GetPocketRadius() const { return TableConfig::POCKET_RADIUS; }
};