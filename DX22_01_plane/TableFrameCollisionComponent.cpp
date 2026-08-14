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

    // Top and bottom rails leave openings at both corners and the center.
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

    // Side rails leave the corner pocket openings exposed.
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
