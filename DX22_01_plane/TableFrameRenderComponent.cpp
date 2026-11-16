#include "TableFrameRenderComponent.h"

#include "Camera.h"
#include "GameBattleView.h"
#include "GameRuntime.h"
#include "Renderer.h"
#include "TableGeometry.h"
#include "TableVisualMesh.h"
#include "TransformComponent.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace DirectX::SimpleMath;

void TableFrameRenderComponent::Awake()
{
    BuildMesh();
	BuildChargeIndicators();

    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
	m_ChargeVertexBuffer.Create(m_ChargeVertices);
	m_ChargeIndexBuffer.Create(m_ChargeIndices);
    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    m_Material = std::make_unique<Material>();
    MATERIAL material{};
    material.Diffuse = Color(1.0f, 1.0f, 1.0f, 1.0f);
    material.TextureEnable = false;
    m_Material->Create(material);

	m_ChargeMaterial = std::make_unique<Material>();
	MATERIAL chargeMaterial{};
	chargeMaterial.Diffuse = Color(1.0f, 0.9f, 0.08f, 1.0f);
	chargeMaterial.Emission = Color(0.65f, 0.48f, 0.0f, 1.0f);
	chargeMaterial.TextureEnable = false;
	m_ChargeMaterial->Create(chargeMaterial);
}

void TableFrameRenderComponent::Draw()
{
    Camera& camera = GameRuntime::MainCamera();
    TransformComponent* transform = GetTransform();
    if (transform == nullptr || m_Material == nullptr)
    {
        return;
    }

    camera.SetCamera();

    Matrix worldMatrix = transform->GetWorldMatrix();
    Renderer::SetWorldMatrix(&worldMatrix);

    ID3D11DeviceContext* deviceContext = Renderer::GetDeviceContext();
    deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    m_Shader.SetGPU();
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();
    m_Material->SetGPU();

    deviceContext->DrawIndexed(
        static_cast<UINT>(m_Indices.size()),
        0,
        0);

	if (m_ChargeMaterial != nullptr)
	{
		m_ChargeVertexBuffer.SetGPU();
		m_ChargeIndexBuffer.SetGPU();
		m_ChargeMaterial->SetGPU();
		for (int region = 0; region < GameBattleView::CushionRegionCount(); ++region)
		{
			if (!GameBattleView::IsCushionChargeActive(region)) continue;
			deviceContext->DrawIndexed(6, static_cast<UINT>(region * 6), 0);
		}
	}
}

void TableFrameRenderComponent::BuildChargeIndicators()
{
	m_ChargeVertices.clear();
	m_ChargeIndices.clear();
	const float outerHalfWidth = TableConfig::TABLE_OUTER_WIDTH * 0.5f;
	const float outerHalfDepth = TableConfig::TABLE_OUTER_DEPTH * 0.5f;
	const float fieldHalfWidth = TableConfig::GetFieldWidth() * 0.5f;
	const float fieldHalfDepth = TableConfig::GetFieldDepth() * 0.5f;
	const float y = TableVisualMesh::RailHeight + 0.025f;
	constexpr float gap = 0.7f;

	// 0..3:上長辺、4..7:下長辺。各辺を4等分する。
	const float longSpan = fieldHalfWidth * 2.0f / 4.0f;
	for (int part = 0; part < 4; ++part)
	{
		const float x0 = -fieldHalfWidth + longSpan * part + gap;
		const float x1 = -fieldHalfWidth + longSpan * (part + 1) - gap;
		AddChargeQuad(x0, outerHalfDepth - 0.25f, x1, outerHalfDepth - 0.05f, y);
	}
	for (int part = 0; part < 4; ++part)
	{
		const float x0 = -fieldHalfWidth + longSpan * part + gap;
		const float x1 = -fieldHalfWidth + longSpan * (part + 1) - gap;
		AddChargeQuad(x0, -outerHalfDepth + 0.05f, x1, -outerHalfDepth + 0.25f, y);
	}

	// 8..9:左短辺、10..11:右短辺。各辺を2等分する。
	const float shortSpan = fieldHalfDepth;
	for (int part = 0; part < 2; ++part)
	{
		const float z0 = -fieldHalfDepth + shortSpan * part + gap;
		const float z1 = -fieldHalfDepth + shortSpan * (part + 1) - gap;
		AddChargeQuad(-outerHalfWidth + 0.05f, z0, -outerHalfWidth + 0.25f, z1, y);
	}
	for (int part = 0; part < 2; ++part)
	{
		const float z0 = -fieldHalfDepth + shortSpan * part + gap;
		const float z1 = -fieldHalfDepth + shortSpan * (part + 1) - gap;
		AddChargeQuad(outerHalfWidth - 0.25f, z0, outerHalfWidth - 0.05f, z1, y);
	}
}

void TableFrameRenderComponent::AddChargeQuad(
	float xMin, float zMin, float xMax, float zMax, float y)
{
	const unsigned int start = static_cast<unsigned int>(m_ChargeVertices.size());
	const Color yellow(1.0f, 0.9f, 0.08f, 1.0f);
	for (const Vector3& position : {
		Vector3(xMin, y, zMax), Vector3(xMax, y, zMax),
		Vector3(xMin, y, zMin), Vector3(xMax, y, zMin) })
	{
		VERTEX_3D vertex{};
		vertex.position = position;
		vertex.normal = Vector3::UnitY;
		vertex.color = yellow;
		m_ChargeVertices.push_back(vertex);
	}
	for (const unsigned int offset : { 0u, 1u, 2u, 2u, 1u, 3u })
		m_ChargeIndices.push_back(start + offset);
}

void TableFrameRenderComponent::BuildMesh()
{
    m_Vertices.clear();
    m_Indices.clear();
    const auto centers = TableConfig::GetPocketCenters();
    m_PocketCenters.assign(centers.begin(), centers.end());

    const auto mesh = TableVisualMesh::BuildFrame();
    m_Vertices.reserve(mesh.size());
    m_Indices.reserve(mesh.size());
    for (const auto& source : mesh)
    {
        VERTEX_3D vertex{};
        vertex.position = Vector3(source.position.x, source.position.y, source.position.z);
        vertex.normal = Vector3(source.normal.x, source.normal.y, source.normal.z);
        vertex.color = Color(source.color.r, source.color.g, source.color.b, 1.0f);
        m_Indices.push_back(static_cast<unsigned int>(m_Vertices.size()));
        m_Vertices.push_back(vertex);
    }
}
