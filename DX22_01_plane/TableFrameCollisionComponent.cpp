#include "TableFrameCollisionComponent.h"

#include "TableConfig.h"

using namespace DirectX::SimpleMath;

void TableFrameCollisionComponent::Awake()
{
    m_Walls.clear();

    const float fieldHalfWidth = TableConfig::GetFieldWidth() * 0.5f;
    const float fieldHalfDepth = TableConfig::GetFieldDepth() * 0.5f;
    const float mouth = TableConfig::POCKET_MOUTH_HALF_WIDTH;
    constexpr float y = 0.0f;

    // 上下のレールは、両端と中央にポケット用の開口部を設ける。
    AddWall(
        Vector3(-fieldHalfWidth + mouth, y, fieldHalfDepth),
        Vector3(-mouth, y, fieldHalfDepth));
    AddWall(
        Vector3(mouth, y, fieldHalfDepth),
        Vector3(fieldHalfWidth - mouth, y, fieldHalfDepth));
    AddWall(
        Vector3(-fieldHalfWidth + mouth, y, -fieldHalfDepth),
        Vector3(-mouth, y, -fieldHalfDepth));
    AddWall(
        Vector3(mouth, y, -fieldHalfDepth),
        Vector3(fieldHalfWidth - mouth, y, -fieldHalfDepth));

    // 左右のレールは、コーナーポケットの開口部を塞がない。
    AddWall(
        Vector3(-fieldHalfWidth, y, -fieldHalfDepth + mouth),
        Vector3(-fieldHalfWidth, y, fieldHalfDepth - mouth));
    AddWall(
        Vector3(fieldHalfWidth, y, -fieldHalfDepth + mouth),
        Vector3(fieldHalfWidth, y, fieldHalfDepth - mouth));
}

void TableFrameCollisionComponent::AddWall(
    const Vector3& start,
    const Vector3& end)
{
    if ((end - start).LengthSquared() <= 0.0001f)
    {
        return;
    }

    m_Walls.push_back({ start, end });
}
