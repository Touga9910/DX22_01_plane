#pragma once

// Lightweight dimensions shared by table gameplay code.
// Vector-based geometry lives in TableGeometry.h.
namespace TableConfig
{
    static constexpr float TABLE_OUTER_WIDTH = 152.0f;
    static constexpr float TABLE_OUTER_DEPTH = 80.0f;
    static constexpr float RAIL_WIDTH = 4.0f;
    static constexpr float POCKET_RADIUS = 3.0f;
    static constexpr float CORNER_POCKET_MOUTH_HALF_WIDTH = 3.5f;
    static constexpr float SIDE_POCKET_MOUTH_HALF_WIDTH = 3.75f;
    static constexpr float FIELD_HEIGHT = 1.0f;
    static constexpr float RAIL_TOP_OFFSET = 0.05f;

    constexpr float GetFieldWidth()
    {
        return TABLE_OUTER_WIDTH - RAIL_WIDTH * 2.0f;
    }

    constexpr float GetFieldDepth()
    {
        return TABLE_OUTER_DEPTH - RAIL_WIDTH * 2.0f;
    }

    constexpr bool IsDepthLongSide()
    {
        return GetFieldDepth() >= GetFieldWidth();
    }
}

static_assert(
    TableConfig::GetFieldWidth() == TableConfig::GetFieldDepth() * 2.0f,
    "The billiard playing surface must keep a 2:1 aspect ratio.");
