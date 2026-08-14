#pragma once

#include <memory>
#include <vector>

#include "Component.h"
#include "IndexBuffer.h"
#include "Material.h"
#include "Shader.h"
#include "Texture.h"
#include "VertexBuffer.h"

// Draws a 2D texture in screen space.
class Texture2D final : public Component
{
public:
	void Awake() override;
	void Draw() override;

	void SetTexture(const char* imageName);

	void SetPosition(float x, float y, float z);
	void SetPosition(const DirectX::SimpleMath::Vector3& position);

	// Rotation values are specified in degrees.
	void SetRotation(float x, float y, float z);
	void SetRotation(const DirectX::SimpleMath::Vector3& rotationDegrees);

	void SetScale(float x, float y, float z);
	void SetScale(const DirectX::SimpleMath::Vector3& scale);

	void SetUV(float numberU, float numberV, float splitX, float splitY);

private:
	std::vector<VERTEX_3D> m_Vertices;
	std::vector<unsigned int> m_Indices;

	IndexBuffer m_IndexBuffer;
	VertexBuffer<VERTEX_3D> m_VertexBuffer;
	Shader m_Shader;
	Texture m_Texture;
	std::unique_ptr<Material> m_Material;

	float m_NumU = 1.0f;
	float m_NumV = 1.0f;
	float m_SplitX = 1.0f;
	float m_SplitY = 1.0f;
};
