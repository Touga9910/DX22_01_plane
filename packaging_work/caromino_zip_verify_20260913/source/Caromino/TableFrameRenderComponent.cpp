#include "TableFrameRenderComponent.h"

#include "Camera.h"
#include "Game.h"
#include "Renderer.h"
#include "TableConfig.h"
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
    Camera* camera = Game::GetCamera();
    TransformComponent* transform = GetTransform();
    if (camera == nullptr || transform == nullptr || m_Material == nullptr)
    {
        return;
    }

    camera->SetCamera();

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

	const auto& charges = Game::GetInstance()->GetCushionCharges();
	if (m_ChargeMaterial != nullptr)
	{
		m_ChargeVertexBuffer.SetGPU();
		m_ChargeIndexBuffer.SetGPU();
		m_ChargeMaterial->SetGPU();
		for (int region = 0; region < CushionChargeRules::RegionCount; ++region)
		{
			if (!charges[static_cast<std::size_t>(region)].active) continue;
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
	const float y = TableConfig::RAIL_TOP_OFFSET + 0.025f;
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
    m_PocketCenters.clear();

    const float outerHalfWidth = TableConfig::TABLE_OUTER_WIDTH * 0.5f;
    const float outerHalfDepth = TableConfig::TABLE_OUTER_DEPTH * 0.5f;
    const float fieldHalfWidth = TableConfig::GetFieldWidth() * 0.5f;
    const float fieldHalfDepth = TableConfig::GetFieldDepth() * 0.5f;
    const float cornerMouth =
        TableConfig::CORNER_POCKET_MOUTH_HALF_WIDTH;
    const float sideMouth = TableConfig::SIDE_POCKET_MOUTH_HALF_WIDTH;
    const float y = TableConfig::RAIL_TOP_OFFSET;
    const Color cushionColor(0.025f, 0.30f, 0.17f, 1.0f);
    const Color pocketColor(0.0f, 0.0f, 0.0f, 1.0f);

    // Build one continuous cushion ring. Its inner edge follows the circular
    // pocket liners, so the holes are actual mesh cut-outs rather than black
    // discs painted over rectangular rails.
    constexpr int curveSteps = 12;
    std::vector<float> xSamples{ -outerHalfWidth, outerHalfWidth };
    const std::array<float, 3> pocketX{
        -fieldHalfWidth, 0.0f, fieldHalfWidth
    };
    const std::array<float, 3> pocketRadii{
        cornerMouth, sideMouth, cornerMouth
    };
    for (std::size_t pocketIndex = 0;
        pocketIndex < pocketX.size(); ++pocketIndex)
    {
        for (int step = -curveSteps; step <= curveSteps; ++step)
        {
            xSamples.push_back(
                pocketX[pocketIndex] +
                pocketRadii[pocketIndex] *
                static_cast<float>(step) /
                static_cast<float>(curveSteps));
        }
    }
    std::sort(xSamples.begin(), xSamples.end());
    xSamples.erase(std::unique(xSamples.begin(), xSamples.end()), xSamples.end());

    const auto topCutoutEdge = [&](float x)
    {
        float edge = fieldHalfDepth;
        for (std::size_t pocketIndex = 0;
            pocketIndex < pocketX.size(); ++pocketIndex)
        {
            const float dx = x - pocketX[pocketIndex];
            const float radius = pocketRadii[pocketIndex];
            if (std::abs(dx) <= radius)
            {
                edge = (std::max)(edge,
                    fieldHalfDepth + std::sqrt((std::max)(
                        0.0f, radius * radius - dx * dx)));
            }
        }
        return edge;
    };

    for (std::size_t index = 0; index + 1 < xSamples.size(); ++index)
    {
        const float x0 = xSamples[index];
        const float x1 = xSamples[index + 1];
        const float top0 = topCutoutEdge(x0);
        const float top1 = topCutoutEdge(x1);
        AddSurfaceQuad(
            Vector3(x0, y, outerHalfDepth),
            Vector3(x1, y, outerHalfDepth),
            Vector3(x0, y, top0),
            Vector3(x1, y, top1),
            cushionColor);
        AddSurfaceQuad(
            Vector3(x0, y, -top0),
            Vector3(x1, y, -top1),
            Vector3(x0, y, -outerHalfDepth),
            Vector3(x1, y, -outerHalfDepth),
            cushionColor);
    }

    std::vector<float> zSamples{ -fieldHalfDepth, fieldHalfDepth };
    for (const float pocketZ : { -fieldHalfDepth, fieldHalfDepth })
    {
        for (int step = -curveSteps; step <= curveSteps; ++step)
        {
            const float z = pocketZ + cornerMouth *
                static_cast<float>(step) /
                static_cast<float>(curveSteps);
            if (z >= -fieldHalfDepth && z <= fieldHalfDepth)
            {
                zSamples.push_back(z);
            }
        }
    }
    std::sort(zSamples.begin(), zSamples.end());
    zSamples.erase(std::unique(zSamples.begin(), zSamples.end()), zSamples.end());

    const auto sideCutoutDepth = [&](float z)
    {
        float depth = 0.0f;
        for (const float pocketZ : { -fieldHalfDepth, fieldHalfDepth })
        {
            const float dz = z - pocketZ;
            if (std::abs(dz) <= cornerMouth)
            {
                depth = (std::max)(depth,
                    std::sqrt((std::max)(0.0f,
                        cornerMouth * cornerMouth - dz * dz)));
            }
        }
        return depth;
    };

    for (std::size_t index = 0; index + 1 < zSamples.size(); ++index)
    {
        const float z0 = zSamples[index];
        const float z1 = zSamples[index + 1];
        const float depth0 = sideCutoutDepth(z0);
        const float depth1 = sideCutoutDepth(z1);
        AddSurfaceQuad(
            Vector3(-outerHalfWidth, y, z1),
            Vector3(-fieldHalfWidth - depth1, y, z1),
            Vector3(-outerHalfWidth, y, z0),
            Vector3(-fieldHalfWidth - depth0, y, z0),
            cushionColor);
        AddSurfaceQuad(
            Vector3(fieldHalfWidth + depth1, y, z1),
            Vector3(outerHalfWidth, y, z1),
            Vector3(fieldHalfWidth + depth0, y, z0),
            Vector3(outerHalfWidth, y, z0),
            cushionColor);
    }

    for (const Vector3& center : TableConfig::GetPocketCenters())
    {
        m_PocketCenters.push_back(center);
        const bool sidePocket = std::abs(center.x) < 0.001f;
        AddPocketDisc(
            center,
            sidePocket ? sideMouth : cornerMouth,
            y + 0.01f,
            pocketColor);
    }
}

void TableFrameRenderComponent::AddSurfaceQuad(
    const Vector3& topLeft,
    const Vector3& topRight,
    const Vector3& bottomLeft,
    const Vector3& bottomRight,
    const Color& color)
{
    const unsigned int startIndex =
        static_cast<unsigned int>(m_Vertices.size());

    VERTEX_3D vertices[4]{};
    vertices[0].position = topLeft;
    vertices[1].position = topRight;
    vertices[2].position = bottomLeft;
    vertices[3].position = bottomRight;
    vertices[0].uv = Vector2(0.0f, 0.0f);
    vertices[1].uv = Vector2(1.0f, 0.0f);
    vertices[2].uv = Vector2(0.0f, 1.0f);
    vertices[3].uv = Vector2(1.0f, 1.0f);
    for (VERTEX_3D& vertex : vertices)
    {
        vertex.color = color;
        vertex.normal = Vector3::UnitY;
        m_Vertices.push_back(vertex);
    }

    m_Indices.push_back(startIndex);
    m_Indices.push_back(startIndex + 1);
    m_Indices.push_back(startIndex + 2);
    m_Indices.push_back(startIndex + 2);
    m_Indices.push_back(startIndex + 1);
    m_Indices.push_back(startIndex + 3);
}

void TableFrameRenderComponent::AddPocketDisc(
    const Vector3& center,
    float radius,
    float y,
    const Color& color)
{
    constexpr int segments = 24;
    constexpr float twoPi = 6.28318530717958647692f;
    for (int segment = 0; segment < segments; segment++)
    {
        const float angle0 = twoPi * static_cast<float>(segment) /
            static_cast<float>(segments);
        const float angle1 = twoPi * static_cast<float>(segment + 1) /
            static_cast<float>(segments);
        const unsigned int startIndex =
            static_cast<unsigned int>(m_Vertices.size());

        VERTEX_3D centerVertex{};
        VERTEX_3D edge0{};
        VERTEX_3D edge1{};
        centerVertex.position = Vector3(center.x, y, center.z);
        edge0.position = Vector3(
            center.x + std::cos(angle0) * radius,
            y,
            center.z + std::sin(angle0) * radius);
        edge1.position = Vector3(
            center.x + std::cos(angle1) * radius,
            y,
            center.z + std::sin(angle1) * radius);
        centerVertex.normal = edge0.normal = edge1.normal = Vector3::UnitY;
        centerVertex.color = edge0.color = edge1.color = color;
        centerVertex.uv = Vector2(0.5f, 0.5f);
        edge0.uv = Vector2(0.0f, 0.0f);
        edge1.uv = Vector2(1.0f, 0.0f);
        m_Vertices.push_back(centerVertex);
        m_Vertices.push_back(edge0);
        m_Vertices.push_back(edge1);
        m_Indices.push_back(startIndex);
        m_Indices.push_back(startIndex + 2);
        m_Indices.push_back(startIndex + 1);
    }
}

void TableFrameRenderComponent::AddQuad(
    float xMin,
    float zMin,
    float xMax,
    float zMax,
    float y,
    const Color& color)
{
    if (xMin >= xMax || zMin >= zMax)
    {
        return;
    }

    const unsigned int startIndex =
        static_cast<unsigned int>(m_Vertices.size());

    VERTEX_3D topLeft{};
    VERTEX_3D topRight{};
    VERTEX_3D bottomLeft{};
    VERTEX_3D bottomRight{};

    topLeft.position = Vector3(xMin, y, zMax);
    topRight.position = Vector3(xMax, y, zMax);
    bottomLeft.position = Vector3(xMin, y, zMin);
    bottomRight.position = Vector3(xMax, y, zMin);

    topLeft.uv = Vector2(0.0f, 0.0f);
    topRight.uv = Vector2(1.0f, 0.0f);
    bottomLeft.uv = Vector2(0.0f, 1.0f);
    bottomRight.uv = Vector2(1.0f, 1.0f);

    topLeft.color = topRight.color = bottomLeft.color = bottomRight.color = color;
    topLeft.normal = topRight.normal = bottomLeft.normal = bottomRight.normal = Vector3::UnitY;

    m_Vertices.push_back(topLeft);
    m_Vertices.push_back(topRight);
    m_Vertices.push_back(bottomLeft);
    m_Vertices.push_back(bottomRight);

    m_Indices.push_back(startIndex + 0);
    m_Indices.push_back(startIndex + 1);
    m_Indices.push_back(startIndex + 2);
    m_Indices.push_back(startIndex + 2);
    m_Indices.push_back(startIndex + 1);
    m_Indices.push_back(startIndex + 3);
}
