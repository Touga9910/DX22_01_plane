#pragma once
#include "Object.h"
#include "Texture.h"
#include "MeshRenderer.h"
#include "StaticMesh.h"
#include "utility.h"
#include "Material.h"

//-----------------------------------------------------------------------------
// Poleクラス
//-----------------------------------------------------------------------------
class Pole :public Object
{
private:

	// 描画の為の情報（メッシュに関わる情報）
	MeshRenderer m_MeshRenderer; // 頂点バッファ・インデックスバッファ・インデックス数

	// 描画の為の情報（見た目に関わる部分）
	std::vector<std::unique_ptr<Material>> m_Materials;
	std::vector<SUBSET> m_subsets;
	std::vector<std::unique_ptr<Texture>> m_Textures; // テクスチャ

	int m_State = 0; // 0:非表示・1:方向選択・2:パワー選択

public:

	void Init();
	void Update();
	void Draw(Camera* cam);
	void Uninit();

	// 位置の設定
	void SetPosition(float x, float y, float z);
	void SetPosition(DirectX::SimpleMath::Vector3 pos);
};

