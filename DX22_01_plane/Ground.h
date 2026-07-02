#pragma once
#include "Object.h"

#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Camera.h"
#include "Shader.h"
#include "Texture.h"
#include "Material.h"
#include "Collision.h"

class Ground : public Object
{
	// SRT情報（姿勢情報）
	DirectX::SimpleMath::Vector3 m_Position = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
	DirectX::SimpleMath::Vector3 m_Rotation = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
	DirectX::SimpleMath::Vector3 m_Scale = DirectX::SimpleMath::Vector3(1.0f, 1.0f, 1.0f);

	// ビリヤード台のサイズ定義（一括管理）
	float m_FieldWidth = 80.0f;  // X軸の幅
	float m_FieldDepth = 145.0f;   // Z軸の奥行き
	float m_FieldHeight = 1.0f;   // ボールが転がるY座標の高さ

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
	/// 高さを取得する関数
	/// </summary>
	/// <returns></returns>
	float GetFieldHeight() const { return m_FieldHeight; }

	/// <summary>
	/// 四方の壁（4本の線分）を配列にして取得する関数
	/// </summary>
	/// <returns></returns>
	std::vector<Collision::Segment> GetWalls() const;

	/// <summary>
	/// 頂点情報を取得
	/// </summary>
	/// <returns></returns>
	std::vector<VERTEX_3D> GetVertices();

	float GetFieldWidth() const { return m_FieldWidth; }
	float GetFieldDepth() const { return m_FieldDepth; }
};

