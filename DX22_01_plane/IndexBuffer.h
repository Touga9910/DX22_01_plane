#pragma once

#include	<limits>
#include	<vector>
#include	<wrl/client.h>
#include	"renderer.h"

using Microsoft::WRL::ComPtr;

//-----------------------------------------------------------------------------
//IndexBufferクラス
//-----------------------------------------------------------------------------
class IndexBuffer {

	ComPtr<ID3D11Buffer> m_IndexBuffer;

public:
	void Create(const std::vector<unsigned int>& indices)
	{
		// 既存のバッファがある場合は解放（再生成に対応）
		m_IndexBuffer.Reset();

		// デバイス取得
		ID3D11Device* device = Renderer::GetDevice();
		assert(device); //deviceが存在することを確認
		
		// インデックスバッファ生成（Renderer.cppから移転）
		D3D11_BUFFER_DESC bd = {};
		bd.Usage = D3D11_USAGE_DEFAULT;								// バッファ使用方
		assert(indices.size() <= (std::numeric_limits<UINT>::max)() / sizeof(unsigned int));
		bd.ByteWidth = static_cast<UINT>(sizeof(unsigned int) * indices.size());				// バッファの大き
		bd.BindFlags = D3D11_BIND_INDEX_BUFFER;						// インデックスバッファ
		bd.CPUAccessFlags = 0;										// CPUアクセス不要

		D3D11_SUBRESOURCE_DATA InitData = {};
		InitData.pSysMem = indices.data();

		HRESULT hr = device->CreateBuffer(&bd, &InitData, m_IndexBuffer.GetAddressOf());

		// 結果を確認
		assert(SUCCEEDED(hr));
		
	}

	void SetGPU()
	{
		// デバイスコンテキスト取得
		ID3D11DeviceContext* devicecontext = Renderer::GetDeviceContext();

		// インデックスバッファをセット
		devicecontext->IASetIndexBuffer(m_IndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	}
};
