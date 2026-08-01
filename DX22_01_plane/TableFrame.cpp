#include "TableFrame.h"
#include "Renderer.h"

#include <cassert>
#include <cmath>
#include <algorithm>

using namespace DirectX::SimpleMath;

void TableFrame::Init()
{
    // Ground と同じ高さに合わせる
    m_Position = Vector3(0.0f, TableConfig::FIELD_HEIGHT, 0.0f);

    BuildMesh();
    BuildWalls();

    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);

    m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    m_Material = std::make_unique<Material>();

    MATERIAL mtrl;
    mtrl.Diffuse = Color(1, 1, 1, 1);
    mtrl.TextureEnable = false;
    m_Material->Create(mtrl);
}

void TableFrame::Update()
{
}

void TableFrame::Draw(Camera* cam)
{
    cam->SetCamera();

    Matrix r = Matrix::CreateFromYawPitchRoll(
        m_Rotation.x,
        m_Rotation.y,
        m_Rotation.z);

    Matrix t = Matrix::CreateTranslation(
        m_Position.x,
        m_Position.y,
        m_Position.z);

    Matrix s = Matrix::CreateScale(
        m_Scale.x,
        m_Scale.y,
        m_Scale.z);

    Matrix worldmtx = s * r * t;
    Renderer::SetWorldMatrix(&worldmtx);

    ID3D11DeviceContext* devicecontext = Renderer::GetDeviceContext();

    devicecontext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    m_Shader.SetGPU();
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();
    m_Material->SetGPU();

    devicecontext->DrawIndexed(
        static_cast<UINT>(m_Indices.size()),
        0,
        0);
}

void TableFrame::Uninit()
{
}

void TableFrame::BuildMesh()
{
    m_Vertices.clear();
    m_Indices.clear();
    m_PocketCenters.clear();

    float outerHalfW = TableConfig::TABLE_OUTER_WIDTH * 0.5f;
    float outerHalfD = TableConfig::TABLE_OUTER_DEPTH * 0.5f;

    float fieldHalfW = TableConfig::GetFieldWidth() * 0.5f;
    float fieldHalfD = TableConfig::GetFieldDepth() * 0.5f;

    float rail = TableConfig::RAIL_WIDTH;
    float pocketRadius = TableConfig::POCKET_RADIUS;

    float cornerGap = pocketRadius * 1.6f;
    float centerGap = pocketRadius * 1.4f;

    float y = TableConfig::RAIL_TOP_OFFSET;

    Color railColor = Color(0.45f, 0.22f, 0.08f, 1.0f);
    Color pocketColor = Color(0.0f, 0.0f, 0.0f, 1.0f);

    // Build four continuous rails without pocket gaps.
    AddQuad(-outerHalfW, fieldHalfD, outerHalfW, outerHalfD, y, railColor);
    AddQuad(-outerHalfW, -outerHalfD, outerHalfW, -fieldHalfD, y, railColor);
    AddQuad(-outerHalfW, -fieldHalfD, -fieldHalfW, fieldHalfD, y, railColor);
    AddQuad(fieldHalfW, -fieldHalfD, outerHalfW, fieldHalfD, y, railColor);
    return;

    if (TableConfig::IsDepthLongSide())
    {
        // =====================================================
        // Z方向が長辺
        // 左右それぞれに、上端・中央・下端ポケット
        // =====================================================

        m_PocketCenters.push_back(Vector3(-fieldHalfW, 0.0f, fieldHalfD));
        m_PocketCenters.push_back(Vector3(-fieldHalfW, 0.0f, 0.0f));
        m_PocketCenters.push_back(Vector3(-fieldHalfW, 0.0f, -fieldHalfD));

        m_PocketCenters.push_back(Vector3(fieldHalfW, 0.0f, fieldHalfD));
        m_PocketCenters.push_back(Vector3(fieldHalfW, 0.0f, 0.0f));
        m_PocketCenters.push_back(Vector3(fieldHalfW, 0.0f, -fieldHalfD));

        // 上側短辺レール
        AddQuad(
            -fieldHalfW + cornerGap,
            fieldHalfD,
            fieldHalfW - cornerGap,
            outerHalfD,
            y,
            railColor);

        // 下側短辺レール
        AddQuad(
            -fieldHalfW + cornerGap,
            -outerHalfD,
            fieldHalfW - cornerGap,
            -fieldHalfD,
            y,
            railColor);

        // 左側長辺レール 下側
        AddQuad(
            -outerHalfW,
            -fieldHalfD + cornerGap,
            -fieldHalfW,
            -centerGap,
            y,
            railColor);

        // 左側長辺レール 上側
        AddQuad(
            -outerHalfW,
            centerGap,
            -fieldHalfW,
            fieldHalfD - cornerGap,
            y,
            railColor);

        // 右側長辺レール 下側
        AddQuad(
            fieldHalfW,
            -fieldHalfD + cornerGap,
            outerHalfW,
            -centerGap,
            y,
            railColor);

        // 右側長辺レール 上側
        AddQuad(
            fieldHalfW,
            centerGap,
            outerHalfW,
            fieldHalfD - cornerGap,
            y,
            railColor);
    }
    else
    {
        // =====================================================
        // X方向が長辺
        // 上下それぞれに、左端・中央・右端ポケット
        // =====================================================

        m_PocketCenters.push_back(Vector3(-fieldHalfW, 0.0f, fieldHalfD));
        m_PocketCenters.push_back(Vector3(0.0f, 0.0f, fieldHalfD));
        m_PocketCenters.push_back(Vector3(fieldHalfW, 0.0f, fieldHalfD));

        m_PocketCenters.push_back(Vector3(-fieldHalfW, 0.0f, -fieldHalfD));
        m_PocketCenters.push_back(Vector3(0.0f, 0.0f, -fieldHalfD));
        m_PocketCenters.push_back(Vector3(fieldHalfW, 0.0f, -fieldHalfD));

        // 上側長辺レール 左側
        AddQuad(
            -fieldHalfW + cornerGap,
            fieldHalfD,
            -centerGap,
            outerHalfD,
            y,
            railColor);

        // 上側長辺レール 右側
        AddQuad(
            centerGap,
            fieldHalfD,
            fieldHalfW - cornerGap,
            outerHalfD,
            y,
            railColor);

        // 下側長辺レール 左側
        AddQuad(
            -fieldHalfW + cornerGap,
            -outerHalfD,
            -centerGap,
            -fieldHalfD,
            y,
            railColor);

        // 下側長辺レール 右側
        AddQuad(
            centerGap,
            -outerHalfD,
            fieldHalfW - cornerGap,
            -fieldHalfD,
            y,
            railColor);

        // 左側短辺レール
        AddQuad(
            -outerHalfW,
            -fieldHalfD + cornerGap,
            -fieldHalfW,
            fieldHalfD - cornerGap,
            y,
            railColor);

        // 右側短辺レール
        AddQuad(
            fieldHalfW,
            -fieldHalfD + cornerGap,
            outerHalfW,
            fieldHalfD - cornerGap,
            y,
            railColor);
    }

    // ポケットの黒い円
    for (const Vector3& center : m_PocketCenters)
    {
        AddDisc(
            center.x,
            center.z,
            pocketRadius,
            32,
            y + 0.01f,
            pocketColor);
    }
}

void TableFrame::BuildWalls()
{
    m_Walls.clear();

    float fieldHalfW = TableConfig::GetFieldWidth() * 0.5f;
    float fieldHalfD = TableConfig::GetFieldDepth() * 0.5f;

    float pocketRadius = TableConfig::POCKET_RADIUS;

    float cornerGap = pocketRadius * 1.6f;
    float centerGap = pocketRadius * 1.4f;

    float y = 0.0f;

    // Build four continuous collision walls without pocket gaps.
    AddWall(
        Vector3{ -fieldHalfW, y, fieldHalfD },
        Vector3{ fieldHalfW, y, fieldHalfD });
    AddWall(
        Vector3{ -fieldHalfW, y, -fieldHalfD },
        Vector3{ fieldHalfW, y, -fieldHalfD });
    AddWall(
        Vector3{ -fieldHalfW, y, -fieldHalfD },
        Vector3{ -fieldHalfW, y, fieldHalfD });
    AddWall(
        Vector3{ fieldHalfW, y, -fieldHalfD },
        Vector3{ fieldHalfW, y, fieldHalfD });
    return;

    if (TableConfig::IsDepthLongSide())
    {
        // =====================================================
        // Z方向が長辺
        // 左右の長辺に3つずつポケット
        // =====================================================

        // 上側短辺の壁
        AddWall(
            Vector3(-fieldHalfW + cornerGap, y, fieldHalfD),
            Vector3(fieldHalfW - cornerGap, y, fieldHalfD));

        // 下側短辺の壁
        AddWall(
            Vector3(-fieldHalfW + cornerGap, y, -fieldHalfD),
            Vector3(fieldHalfW - cornerGap, y, -fieldHalfD));

        // 左側長辺 下側
        AddWall(
            Vector3(-fieldHalfW, y, -fieldHalfD + cornerGap),
            Vector3(-fieldHalfW, y, -centerGap));

        // 左側長辺 上側
        AddWall(
            Vector3(-fieldHalfW, y, centerGap),
            Vector3(-fieldHalfW, y, fieldHalfD - cornerGap));

        // 右側長辺 下側
        AddWall(
            Vector3(fieldHalfW, y, -fieldHalfD + cornerGap),
            Vector3(fieldHalfW, y, -centerGap));

        // 右側長辺 上側
        AddWall(
            Vector3(fieldHalfW, y, centerGap),
            Vector3(fieldHalfW, y, fieldHalfD - cornerGap));
    }
    else
    {
        // =====================================================
        // X方向が長辺
        // 上下の長辺に3つずつポケット
        // =====================================================

        // 上側長辺 左側
        AddWall(
            Vector3(-fieldHalfW + cornerGap, y, fieldHalfD),
            Vector3(-centerGap, y, fieldHalfD));

        // 上側長辺 右側
        AddWall(
            Vector3(centerGap, y, fieldHalfD),
            Vector3(fieldHalfW - cornerGap, y, fieldHalfD));

        // 下側長辺 左側
        AddWall(
            Vector3(-fieldHalfW + cornerGap, y, -fieldHalfD),
            Vector3(-centerGap, y, -fieldHalfD));

        // 下側長辺 右側
        AddWall(
            Vector3(centerGap, y, -fieldHalfD),
            Vector3(fieldHalfW - cornerGap, y, -fieldHalfD));

        // 左側短辺
        AddWall(
            Vector3(-fieldHalfW, y, -fieldHalfD + cornerGap),
            Vector3(-fieldHalfW, y, fieldHalfD - cornerGap));

        // 右側短辺
        AddWall(
            Vector3(fieldHalfW, y, -fieldHalfD + cornerGap),
            Vector3(fieldHalfW, y, fieldHalfD - cornerGap));
    }
}

void TableFrame::AddQuad(
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

    unsigned int startIndex = static_cast<unsigned int>(m_Vertices.size());

    VERTEX_3D v0;
    VERTEX_3D v1;
    VERTEX_3D v2;
    VERTEX_3D v3;

    // Ground と同じ向きで上面を作る
    v0.position = Vector3(xMin, y, zMax);
    v1.position = Vector3(xMax, y, zMax);
    v2.position = Vector3(xMin, y, zMin);
    v3.position = Vector3(xMax, y, zMin);

    v0.color = color;
    v1.color = color;
    v2.color = color;
    v3.color = color;

    v0.uv = Vector2(0.0f, 0.0f);
    v1.uv = Vector2(1.0f, 0.0f);
    v2.uv = Vector2(0.0f, 1.0f);
    v3.uv = Vector2(1.0f, 1.0f);

    v0.normal = Vector3(0.0f, 1.0f, 0.0f);
    v1.normal = Vector3(0.0f, 1.0f, 0.0f);
    v2.normal = Vector3(0.0f, 1.0f, 0.0f);
    v3.normal = Vector3(0.0f, 1.0f, 0.0f);

    m_Vertices.push_back(v0);
    m_Vertices.push_back(v1);
    m_Vertices.push_back(v2);
    m_Vertices.push_back(v3);

    m_Indices.push_back(startIndex + 0);
    m_Indices.push_back(startIndex + 1);
    m_Indices.push_back(startIndex + 2);

    m_Indices.push_back(startIndex + 2);
    m_Indices.push_back(startIndex + 1);
    m_Indices.push_back(startIndex + 3);
}

void TableFrame::AddDisc(
    float centerX,
    float centerZ,
    float radius,
    int split,
    float y,
    const Color& color)
{
    if (split < 3)
    {
        return;
    }

    const float PI = 3.1415926535f;

    unsigned int centerIndex = static_cast<unsigned int>(m_Vertices.size());

    VERTEX_3D centerVertex;
    centerVertex.position = Vector3(centerX, y, centerZ);
    centerVertex.color = color;
    centerVertex.uv = Vector2(0.5f, 0.5f);
    centerVertex.normal = Vector3(0.0f, 1.0f, 0.0f);

    m_Vertices.push_back(centerVertex);

    for (int i = 0; i <= split; i++)
    {
        float angle = (2.0f * PI * i) / static_cast<float>(split);

        VERTEX_3D v;
        v.position = Vector3(
            centerX + std::cos(angle) * radius,
            y,
            centerZ + std::sin(angle) * radius);

        v.color = color;
        v.uv = Vector2(
            0.5f + std::cos(angle) * 0.5f,
            0.5f + std::sin(angle) * 0.5f);

        v.normal = Vector3(0.0f, 1.0f, 0.0f);

        m_Vertices.push_back(v);
    }

    for (int i = 1; i <= split; i++)
    {
        m_Indices.push_back(centerIndex);
        m_Indices.push_back(centerIndex + i + 1);
        m_Indices.push_back(centerIndex + i);
    }
}

void TableFrame::AddWall(
    const Vector3& start,
    const Vector3& end)
{
    if ((end - start).LengthSquared() <= 0.0001f)
    {
        return;
    }

    m_Walls.push_back({ start, end });
}

std::vector<Collision::Segment> TableFrame::GetWalls() const
{
    std::vector<Collision::Segment> result;
    result.reserve(m_Walls.size());

    Matrix r = Matrix::CreateFromYawPitchRoll(
        m_Rotation.x,
        m_Rotation.y,
        m_Rotation.z);

    Matrix t = Matrix::CreateTranslation(
        m_Position.x,
        m_Position.y,
        m_Position.z);

    Matrix s = Matrix::CreateScale(
        m_Scale.x,
        m_Scale.y,
        m_Scale.z);

    Matrix worldmtx = s * r * t;

    for (const Collision::Segment& wall : m_Walls)
    {
        Collision::Segment transformedWall;
        transformedWall.start = Vector3::Transform(wall.start, worldmtx);
        transformedWall.end = Vector3::Transform(wall.end, worldmtx);

        result.push_back(transformedWall);
    }

    return result;
}

/// <summary>
/// ポケットの判定用の球を取得する関数
/// </summary>
/// <returns>ポケットの中心座標と半径</returns>
std::vector<Collision::Sphere> TableFrame::GetPocketSpheres() const
{
    std::vector<Collision::Sphere> result;
    result.reserve(m_PocketCenters.size());

    Matrix r = Matrix::CreateFromYawPitchRoll(
        m_Rotation.x,
        m_Rotation.y,
        m_Rotation.z);

    Matrix t = Matrix::CreateTranslation(
        m_Position.x,
        m_Position.y,
        m_Position.z);

    Matrix s = Matrix::CreateScale(
        m_Scale.x,
        m_Scale.y,
        m_Scale.z);

    Matrix worldmtx = s * r * t;

    float maxScale = max(m_Scale.x, max(m_Scale.y, m_Scale.z));

    for (const Vector3& center : m_PocketCenters)
    {
        Collision::Sphere pocket;
        pocket.center = Vector3::Transform(center, worldmtx);
        pocket.radius = TableConfig::POCKET_RADIUS * maxScale;

        result.push_back(pocket);
    }

    return result;
}
