#pragma once

#include	<simplemath.h>
#include	<string>
#include	<vector>
#include	<memory>
#include	"Texture.h"
#include	"Mesh.h"
#include	"renderer.h"

class StaticMesh : public Mesh {
public:
	void Load(std::string filename, std::string texturedirectory="");

	/// <summary>
	/// 読み込んだマテリアルの一覧を読み取り専用の参照で返す。
	/// </summary>
	const std::vector<MATERIAL>& GetMaterials() {
		return m_materials;
	}

	/// <summary>
	/// 読み込んだサブセットの一覧を読み取り専用の参照で返す。
	/// </summary>
	const std::vector<SUBSET>& GetSubsets() {
		return m_subsets;
	}

	/// <summary>
	/// マテリアルに対応するテクスチャ名の一覧を読み取り専用の参照で返す。
	/// </summary>
	const std::vector<std::string>& GetTextureNames() {
		return m_texturenames;
	}

	/// <summary>
	/// 読み込んだテクスチャを、所有権を移して返す。
	/// </summary>
	std::vector<std::unique_ptr<Texture>> GetTextures() {
		return std::move(m_textures);
	}

private:

	std::vector<MATERIAL> m_materials;	    // マテリアル情報
	std::vector<std::string> m_texturenames;			// テクスチャ名
	std::vector<SUBSET> m_subsets;						// サブセット情報
	std::vector<std::unique_ptr<Texture>>	m_textures;	// テクスチャ群
};