#include "TableFrameCollisionComponent.h"

#include "TableConfig.h"

using namespace DirectX::SimpleMath;

/// <summary>
/// 台枠のローカル座標で衝突線分を生成して保存する。
/// </summary>
void TableFrameCollisionComponent::Awake()
{
    m_Walls = BuildLocalWalls();
}

/// <summary>
/// 台の寸法とポケットの開口幅から、枠や開口部の縁の衝突線分を組み立てる。
/// </summary>
std::vector<Collision::Segment>
TableFrameCollisionComponent::BuildLocalWalls()
{
    std::vector<Collision::Segment> walls;

    const float fieldHalfWidth = TableConfig::GetFieldWidth() * 0.5f;
    const float fieldHalfDepth = TableConfig::GetFieldDepth() * 0.5f;
    const float cornerMouth =
        TableConfig::CORNER_POCKET_MOUTH_HALF_WIDTH;
    const float sideMouth = TableConfig::SIDE_POCKET_MOUTH_HALF_WIDTH;
    const float cornerRearWallX = fieldHalfWidth + cornerMouth;
    const float cornerRearWallZ = fieldHalfDepth + cornerMouth;
    const float sideRearWallZ = fieldHalfDepth + sideMouth;
    constexpr float y = 0.0f;

    // Cushion noses. Side-pocket mouths are wider than corner mouths.
    AddWall(
        walls,
        Vector3(-fieldHalfWidth + cornerMouth, y, fieldHalfDepth),
        Vector3(-sideMouth, y, fieldHalfDepth));
    AddWall(
        walls,
        Vector3(sideMouth, y, fieldHalfDepth),
        Vector3(fieldHalfWidth - cornerMouth, y, fieldHalfDepth));
    AddWall(
        walls,
        Vector3(-fieldHalfWidth + cornerMouth, y, -fieldHalfDepth),
        Vector3(-sideMouth, y, -fieldHalfDepth));
    AddWall(
        walls,
        Vector3(sideMouth, y, -fieldHalfDepth),
        Vector3(fieldHalfWidth - cornerMouth, y, -fieldHalfDepth));

    AddWall(
        walls,
        Vector3(-fieldHalfWidth, y, -fieldHalfDepth + cornerMouth),
        Vector3(-fieldHalfWidth, y, fieldHalfDepth - cornerMouth));
    AddWall(
        walls,
        Vector3(fieldHalfWidth, y, -fieldHalfDepth + cornerMouth),
        Vector3(fieldHalfWidth, y, fieldHalfDepth - cornerMouth));

    // Rear cushions close every pocket throat on the outside of the table.
    // A correctly aimed ball reaches the pocket trigger first; a near miss is
    // caught here instead of escaping through the old open rail geometry.
    AddWall(walls,
        Vector3(-cornerRearWallX, y, cornerRearWallZ),
        Vector3(-fieldHalfWidth + cornerMouth, y, cornerRearWallZ));
    AddWall(walls,
        Vector3(-sideMouth, y, sideRearWallZ),
        Vector3(sideMouth, y, sideRearWallZ));
    AddWall(walls,
        Vector3(fieldHalfWidth - cornerMouth, y, cornerRearWallZ),
        Vector3(cornerRearWallX, y, cornerRearWallZ));

    AddWall(walls,
        Vector3(-cornerRearWallX, y, -cornerRearWallZ),
        Vector3(-fieldHalfWidth + cornerMouth, y, -cornerRearWallZ));
    AddWall(walls,
        Vector3(-sideMouth, y, -sideRearWallZ),
        Vector3(sideMouth, y, -sideRearWallZ));
    AddWall(walls,
        Vector3(fieldHalfWidth - cornerMouth, y, -cornerRearWallZ),
        Vector3(cornerRearWallX, y, -cornerRearWallZ));

    AddWall(walls,
        Vector3(-cornerRearWallX, y, -cornerRearWallZ),
        Vector3(-cornerRearWallX, y, -fieldHalfDepth + cornerMouth));
    AddWall(walls,
        Vector3(-cornerRearWallX, y, fieldHalfDepth - cornerMouth),
        Vector3(-cornerRearWallX, y, cornerRearWallZ));
    AddWall(walls,
        Vector3(cornerRearWallX, y, -cornerRearWallZ),
        Vector3(cornerRearWallX, y, -fieldHalfDepth + cornerMouth));
    AddWall(walls,
        Vector3(cornerRearWallX, y, fieldHalfDepth - cornerMouth),
        Vector3(cornerRearWallX, y, cornerRearWallZ));

    return walls;
}

/// <summary>
/// 十分な長さのある線分を壁の当たり判定に追加する。
/// </summary>
void TableFrameCollisionComponent::AddWall(
    std::vector<Collision::Segment>& walls,
    const Vector3& start,
    const Vector3& end)
{
    if ((end - start).LengthSquared() <= 0.0001f)
    {
        return;
    }

    walls.push_back({ start, end });
}
