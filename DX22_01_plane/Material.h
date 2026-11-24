#pragma once
#include	<Windows.h>
#include	<directxmath.h>
#include	<d3d11.h>
#include	<wrl/client.h>
#include	"renderer.h"

class Material {

	struct ConstantBufferMaterial {
		MATERIAL	Material;
	};

	MATERIAL	m_Material{};
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_pConstantBufferMaterial;

public:
	/// <summary>
	/// マテリアルを既定の状態で構築する。
	/// </summary>
	Material() {}

	/// <summary>
	/// 指定されたマテリアル情報を使って定数バッファを作成する。
	/// </summary>
	Material(MATERIAL mtrl) {
		Create(mtrl);
	}

	/// <summary>
	/// 終了処理を呼び出し、保持している定数バッファの所有権を解放する。
	/// </summary>
	~Material() {
		Uninit();
	}

	/// <summary>
	/// 定数バッファを作成し、指定されたマテリアル情報をGPUに反映する。
	/// </summary>
	bool Create(MATERIAL mtrl) {

		ID3D11Device* dev;
		dev = Renderer::GetDevice();

		// コンスタントバッファ作成
		bool sts = Renderer::CreateConstantBuffer(
			sizeof(ConstantBufferMaterial),		// サイズ
			m_pConstantBufferMaterial.GetAddressOf());		// コンスタントバッファ４
		if (!sts) {
			MessageBox(NULL, "CreateBuffer(constant buffer Material) error", "Error", MB_OK);
			return false;
		}

		m_Material.Ambient = mtrl.Ambient;
		m_Material.Diffuse = mtrl.Diffuse;
		m_Material.Specular = mtrl.Specular;
		m_Material.Emission = mtrl.Emission;
		m_Material.Shiness = mtrl.Shiness;
		m_Material.TextureEnable = mtrl.TextureEnable;

		Update();

		return true;
	}

	/// <summary>
	/// 保持しているマテリアル情報を定数バッファに書き込み、頂点シェーダーとピクセルシェーダーに設定する。
	/// </summary>
	void Update() {
		ConstantBufferMaterial		cb{};

		cb.Material = m_Material;

		ID3D11DeviceContext* devcontext;
		devcontext = Renderer::GetDeviceContext();

		devcontext->UpdateSubresource(
			m_pConstantBufferMaterial.Get(),
			0,
			nullptr,
			&cb,
			0, 0);

		// コンスタントバッファ4をｂ4レジスタへセット（頂点シェーダー用）
		devcontext->VSSetConstantBuffers(4, 1, m_pConstantBufferMaterial.GetAddressOf());

		// コンスタントバッファ4をｂ4レジスタへセット(ピクセルシェーダー用)
		devcontext->PSSetConstantBuffers(4, 1, m_pConstantBufferMaterial.GetAddressOf());

	}

	/// <summary>
	/// マテリアルの定数バッファを頂点シェーダーとピクセルシェーダーのスロット4に設定する。
	/// </summary>
	void SetGPU() {

		ID3D11DeviceContext* devcontext;
		devcontext = Renderer::GetDeviceContext();

		// コンスタントバッファ4をｂ4レジスタへセット（頂点シェーダー用）
		devcontext->VSSetConstantBuffers(4, 1, m_pConstantBufferMaterial.GetAddressOf());

		// コンスタントバッファ4をｂ4レジスタへセット(ピクセルシェーダー用)
		devcontext->PSSetConstantBuffers(4, 1, m_pConstantBufferMaterial.GetAddressOf());
	}

	/// <summary>
	/// 指定されたマテリアル情報を定数バッファに書き込み、両方のシェーダーに設定する。
	/// </summary>
	void SetMaterial(const MATERIAL& mtrl) {
		ConstantBufferMaterial		cb{};

		cb.Material = mtrl;

		ID3D11DeviceContext* devcontext;
		devcontext = Renderer::GetDeviceContext();

		devcontext->UpdateSubresource(
			m_pConstantBufferMaterial.Get(),
			0,
			nullptr,
			&cb,
			0, 0);

		// コンスタントバッファ4をｂ4レジスタへセット（頂点シェーダー用）
		devcontext->VSSetConstantBuffers(4, 1, m_pConstantBufferMaterial.GetAddressOf());

		// コンスタントバッファ4をｂ4レジスタへセット(ピクセルシェーダー用)
		devcontext->PSSetConstantBuffers(4, 1, m_pConstantBufferMaterial.GetAddressOf());

	}

	/// <summary>
	/// 終了処理の呼び出し口を提供する。定数バッファはスマートポインターが管理する。
	/// </summary>
	void Uninit() {
	}

	/// <summary>
	/// 保持しているマテリアルの拡散反射色を設定する。
	/// </summary>
	void SetDiffuse(DirectX::XMFLOAT4 diffuse) {
		m_Material.Diffuse = diffuse;
	}

	/// <summary>
	/// 保持しているマテリアルの環境光反射色を設定する。
	/// </summary>
	void SetAmbient(DirectX::XMFLOAT4 ambient) {
		m_Material.Ambient = ambient;
	}

	/// <summary>
	/// 保持しているマテリアルの鏡面反射色を設定する。
	/// </summary>
	void SetSpecular(DirectX::XMFLOAT4 specular) {
		m_Material.Specular = specular;
	}

	/// <summary>
	/// 保持しているマテリアルの発光色を設定する。
	/// </summary>
	void SetEmission(DirectX::XMFLOAT4 emission) {
		m_Material.Emission = emission;
	}

	/// <summary>
	/// 保持しているマテリアルの光沢度を設定する。
	/// </summary>
	void SetShiness(float shiness) {
		m_Material.Shiness = shiness;
	}

	/// <summary>
	/// 保持しているマテリアルでテクスチャの使用が有効かを返す。
	/// </summary>
	bool isTextureEnable() {
		return m_Material.TextureEnable == TRUE;
	}
};