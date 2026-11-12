#pragma once
#include	<wrl/client.h>
#include	<string>
#include	<d3d11.h>

using Microsoft::WRL::ComPtr;

//-----------------------------------------------------------------------------
// Shader wrapper
//-----------------------------------------------------------------------------
class Shader{
public:
	// Keep the original ABI for existing render components and incremental builds.
	void Create(std::string vs, std::string ps);
	bool CreateChecked(const std::string& vs, const std::string& ps);
	void SetGPU();
private:
	ComPtr<ID3D11VertexShader> m_pVertexShader;
	ComPtr<ID3D11PixelShader>  m_pPixelShader;
	ComPtr<ID3D11InputLayout>  m_pVertexLayout;
};

