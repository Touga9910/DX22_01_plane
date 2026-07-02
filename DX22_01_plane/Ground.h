#pragma once
#include "Object.h"

#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Camera.h"
#include "Shader.h"
#include "Texture.h"
#include "Material.h"
#include "Collision.h"

#include "TableConfig.h"

class Ground : public Object
{
	// SRT情報（姿勢情報）
	DirectX::SimpleMath::Vector3 m_Position = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
	DirectX::SimpleMath::Vector3 m_Rotation = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
	DirectX::SimpleMath::Vector3 m_Scale = DirectX::SimpleMath::Vector3(1.0f, 1.0f, 1.0f);

	// 頂点データ
	std::vector<VERTEX_3D> m_Vertices;

	//インデックスデータ
	std::vector<unsigned int> m_Indices;

	// 描画の為の情報（メッシュに関わる情報）
	IndexBuffer	 m_IndexBuffer; // インデックスバッファ
	VertexBuffer<VERTEX_3D>	m_VertexBuffer; // 頂点バッファ

	// 描画の為の情報（見た目に関わる部分）
	Shader m_Shader; // シェーダー

	Texture m_Texture;	//テクスチャ

	std::unique_ptr<Material> m_Material;//マテリアル

	int m_SizeX = 0;	//縦サイズ
	int m_SizeZ = 0;	//横サイズ

public:
	void Init();
	void Update();
	void Draw(Camera* cam);
	void Uninit();

	/// <summary>
	/// 頂点情報を取得
	/// </summary>
	/// <returns></returns>
	std::vector<VERTEX_3D> GetVertices();
	
	// コンフィグから必要情報を取得
	float GetFieldHeight() const { return TableConfig::FIELD_HEIGHT; }
	float GetFieldWidth() const { return TableConfig::GetFieldWidth(); }
	float GetFieldDepth() const { return TableConfig::GetFieldDepth(); }
};

