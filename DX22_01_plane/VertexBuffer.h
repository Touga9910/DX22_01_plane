#pragma once
#include	<vector>
#include	<wrl/client.h>
#include	"renderer.h"

using Microsoft::WRL::ComPtr;

//-----------------------------------------------------------------------------
//VertexBufferクラス
//-----------------------------------------------------------------------------
template <typename T> class VertexBuffer{

	ComPtr<ID3D11Buffer> m_VertexBuffer;

public:
	void Create(const std::vector<T>& vertices)
	{
		//既存のバッファがある場合は解放（再生成に対応）
		m_VertexBuffer.Reset();

		// デバイス取得
		ID3D11Device* device = nullptr;
		device = Renderer::GetDevice();
		assert(device); //deviceは存在することを確認

		// 頂点バッファ作成（Renderer.cppから移転）
		D3D11_BUFFER_DESC bd = {};
		// Modifyメソッドで書き換えるため、USAGE_DYNAMIC と CPU_ACCESS_WRITE を指定
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.ByteWidth = (UINT)(sizeof(T) * vertices.size());
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bd.MiscFlags = 0;
		bd.StructureByteStride = 0;

		// 3. 初期データの準備（Subresource Data）
		D3D11_SUBRESOURCE_DATA initData = {};
		initData.pSysMem = vertices.data();

		// 4. バッファ生成
		HRESULT hr = device->CreateBuffer(&bd, &initData, m_VertexBuffer.GetAddressOf());
		assert(SUCCEEDED(hr));
	}

	// GPUにセット
	void SetGPU()
	{
		// デバイスコンテキスト取得
		ID3D11DeviceContext* devicecontext = nullptr;
		devicecontext = Renderer::GetDeviceContext();

		// 頂点バッファをセットする
		unsigned int stride = sizeof(T);
		unsigned  offset = 0;
		devicecontext->IASetVertexBuffers(0, 1, m_VertexBuffer.GetAddressOf(), &stride, &offset);
	}

	// 頂点バッファを書き換える
	void Modify(const std::vector<T>& vertices)
	{
		//頂点データ書き換え
		D3D11_MAPPED_SUBRESOURCE msr;
		HRESULT hr = Renderer::GetDeviceContext()->Map(
			m_VertexBuffer.Get(), 
			0,
			D3D11_MAP_WRITE_DISCARD, 0, &msr);

		if (SUCCEEDED(hr)) {
			memcpy(msr.pData, vertices.data(), vertices.size() * sizeof(T));
			Renderer::GetDeviceContext()->Unmap(m_VertexBuffer.Get(), 0);
		}
	}
};
