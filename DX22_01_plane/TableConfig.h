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

    /// <summary>
    /// 台の外幅から左右の枠の幅を引き、盤面の幅を求める。
    /// </summary>
    constexpr float GetFieldWidth()
    {
        return TABLE_OUTER_WIDTH - RAIL_WIDTH * 2.0f;
    }

    /// <summary>
    /// 台の外形の奥行きから手前と奥の枠の幅を引き、盤面の奥行きを求める。
    /// </summary>
    constexpr float GetFieldDepth()
    {
        return TABLE_OUTER_DEPTH - RAIL_WIDTH * 2.0f;
    }

    /// <summary>
    /// 盤面の奥行きが幅以上かを判定する。
    /// </summary>
    constexpr bool IsDepthLongSide()
    {
        return GetFieldDepth() >= GetFieldWidth();
    }
}

static_assert(
    TableConfig::GetFieldWidth() == TableConfig::GetFieldDepth() * 2.0f,
    "The billiard playing surface must keep a 2:1 aspect ratio.");
