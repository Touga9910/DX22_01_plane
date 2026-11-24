#include	"Shader.h"
#include	"renderer.h"

/// <summary>
/// 指定されたファイルから頂点シェーダー、入力レイアウト、ピクセルシェーダーを作成する。
/// </summary>
void Shader::Create(std::string vs, std::string ps)
{
	CreateChecked(vs, ps);
}

/// <summary>
/// 既存のシェーダーを解放して指定ファイルから作り直し、作成の成否を返す。
/// </summary>
bool Shader::CreateChecked(const std::string& vs, const std::string& ps)
{
	m_pVertexShader.Reset();
	m_pPixelShader.Reset();
	m_pVertexLayout.Reset();

	// 頂点入力レイアウトをVERTEX_3DとHLSLの入力定義に合わせる。
	D3D11_INPUT_ELEMENT_DESC layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,		0,	D3D11_APPEND_ALIGNED_ELEMENT,	D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,		0,	D3D11_APPEND_ALIGNED_ELEMENT,	D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT,	0,	D3D11_APPEND_ALIGNED_ELEMENT,	D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,			0,	D3D11_APPEND_ALIGNED_ELEMENT,   D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};

	unsigned int numElements = ARRAYSIZE(layout);

	// 同じバイトコードから頂点シェーダーと入力レイアウトを作成する。
	HRESULT hr = Renderer::CreateVertexShader(
		&m_pVertexShader,
		&m_pVertexLayout,
		layout,
		numElements,
		vs.c_str()
		);
	if (FAILED(hr)) {
		char message[1024]{};
		sprintf_s(message, "CreateVertexShader/CreateInputLayout failed.\nFile: %s\nHRESULT: 0x%08X",
			vs.c_str(), static_cast<unsigned int>(hr));
		OutputDebugStringA(message);
		OutputDebugStringA("\n");
		MessageBoxA(nullptr, message, "Shader creation error", MB_OK | MB_ICONERROR);
		return false;
	}

	// ピクセルシェーダーを作成する。

	hr = Renderer::CreatePixelShader(
		&m_pPixelShader,
		ps.c_str()
		);
	if (FAILED(hr)) {
		char message[1024]{};
		sprintf_s(message, "CreatePixelShader failed.\nFile: %s\nHRESULT: 0x%08X",
			ps.c_str(), static_cast<unsigned int>(hr));
		OutputDebugStringA(message);
		OutputDebugStringA("\n");
		MessageBoxA(nullptr, message, "Shader creation error", MB_OK | MB_ICONERROR);
		m_pVertexShader.Reset();
		m_pVertexLayout.Reset();
		return false;
	}

	return true;
}

/// <summary>
/// このオブジェクトのシェーダーと頂点入力レイアウトを描画パイプラインに設定する。
/// </summary>
void Shader::SetGPU()
{
	ID3D11DeviceContext* devicecontext = Renderer::GetDeviceContext();

	devicecontext->VSSetShader(m_pVertexShader.Get(), nullptr, 0);
	devicecontext->PSSetShader(m_pPixelShader.Get(), nullptr, 0);
	devicecontext->IASetInputLayout(m_pVertexLayout.Get());
}

