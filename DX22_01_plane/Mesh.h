#pragma once
#include	<vector>
#include	"renderer.h"

class Mesh {
protected:
	std::vector<VERTEX_3D>		m_vertices;		// 頂点座標群
	std::vector<unsigned int>	m_indices;		// インデックスデータ群
public:
	/// <summary>
	/// メッシュの頂点データを読み取り専用の参照で返す。
	/// </summary>
	const std::vector<VERTEX_3D>& GetVertices() {
		return m_vertices;
	}

	/// <summary>
	/// メッシュのインデックスを読み取り専用の参照で返す。
	/// </summary>
	const std::vector<unsigned int>& GetIndices() {
		return m_indices;
	}
};

