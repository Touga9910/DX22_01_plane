#include "Texture2D.h"

#include <cassert>

#include "Camera.h"
#include "Game.h"
#include "Renderer.h"
#include "TransformComponent.h"

using namespace DirectX::SimpleMath;

void Texture2D::Awake()
{
	m_Vertices.resize(4);

	m_Vertices[0].position = Vector3(-0.5f, 0.5f, 0.0f);
	m_Vertices[1].position = Vector3(0.5f, 0.5f, 0.0f);
	m_Vertices[2].position = Vector3(-0.5f, -0.5f, 0.0f);
	m_Vertices[3].position = Vector3(0.5f, -0.5f, 0.0f);

	for (VERTEX_3D& vertex : m_Vertices)
	{
		vertex.color = Color(1.0f, 1.0f, 1.0f, 1.0f);
	}

	m_Vertices[0].uv = Vector2(0.0f, 0.0f);
	m_Vertices[1].uv = Vector2(1.0f, 0.0f);
	m_Vertices[2].uv = Vector2(0.0f, 1.0f);
	m_Vertices[3].uv = Vector2(1.0f, 1.0f);

	m_VertexBuffer.Create(m_Vertices);

	m_Indices = { 0, 1, 2, 3 };
	m_IndexBuffer.Create(m_Indices);

	m_Shader.Create(
		"shader/unlitTextureVS.hlsl",
		"shader/unlitTexturePS.hlsl");

	m_Material = std::make_unique<Material>();
	MATERIAL material{};
	material.Diffuse = Color(1.0f, 1.0f, 1.0f, 1.0f);
	material.TextureEnable = true;
	m_Material->Create(material);
}

void Texture2D::Draw()
{
	Camera* camera = Game::GetCamera();
	TransformComponent* transform = GetTransform();
	if (camera == nullptr || transform == nullptr || m_Material == nullptr)
	{
		return;
	}

	camera->SetCamera(1);

	Matrix worldMatrix = transform->GetWorldMatrix();
	Renderer::SetWorldMatrix(&worldMatrix);

	ID3D11DeviceContext* deviceContext = Renderer::GetDeviceContext();
	deviceContext->IASetPrimitiveTopology(
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	m_Shader.SetGPU();
	m_VertexBuffer.SetGPU();
	m_IndexBuffer.SetGPU();
	m_Texture.SetGPU();
	m_Material->SetGPU();

	const float u = m_NumU - 1.0f;
	const float v = m_NumV - 1.0f;
	const float width = 1.0f / m_SplitX;
	const float height = 1.0f / m_SplitY;
	Renderer::SetUV(u, v, width, height);

	deviceContext->DrawIndexed(
		static_cast<UINT>(m_Indices.size()),
		0,
		0);
}

void Texture2D::SetTexture(const char* imageName)
{
	const bool loaded = m_Texture.Load(imageName);
	assert(loaded);
}

void Texture2D::SetPosition(float x, float y, float z)
{
	SetPosition(Vector3(x, y, z));
}

void Texture2D::SetPosition(const Vector3& position)
{
	if (TransformComponent* transform = GetTransform())
	{
		transform->SetPosition(position);
	}
}

void Texture2D::SetRotation(float x, float y, float z)
{
	SetRotation(Vector3(x, y, z));
}

void Texture2D::SetRotation(const Vector3& rotationDegrees)
{
	if (TransformComponent* transform = GetTransform())
	{
		const Vector3 radians =
			rotationDegrees * (DirectX::XM_PI / 180.0f);

		transform->SetRotation(
			Quaternion::CreateFromYawPitchRoll(
				radians.y,
				radians.x,
				radians.z));
	}
}

void Texture2D::SetScale(float x, float y, float z)
{
	SetScale(Vector3(x, y, z));
}

void Texture2D::SetScale(const Vector3& scale)
{
	if (TransformComponent* transform = GetTransform())
	{
		transform->SetScale(scale);
	}
}

void Texture2D::SetUV(
	float numberU,
	float numberV,
	float splitX,
	float splitY)
{
	m_NumU = numberU;
	m_NumV = numberV;
	m_SplitX = splitX;
	m_SplitY = splitY;
}
