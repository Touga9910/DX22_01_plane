#pragma once

namespace TableConfig
{
    // ==========================
    // ビリヤード台全体の外形サイズ
    // レールを含めたサイズ
    // ==========================
    static const float TABLE_OUTER_WIDTH = 145.0f;   // X方向
    static const float TABLE_OUTER_DEPTH = 80.0f;  // Z方向

    // ==========================
    // レール・ポケット
    // ==========================
    static const float RAIL_WIDTH = 4.0f;
    static const float POCKET_RADIUS = 2.0f;

    // ==========================
    // 高さ
    // ==========================
    static const float FIELD_HEIGHT = 1.0f;
    static const float RAIL_TOP_OFFSET = 0.05f;

    // ==========================
    // 実際にボールが転がる地面サイズ
    // TableFrame全体 - レール幅 * 2
    // ==========================
    static float GetFieldWidth()
    {
        return TABLE_OUTER_WIDTH - RAIL_WIDTH * 2.0f;
    }

    static float GetFieldDepth()
    {
        return TABLE_OUTER_DEPTH - RAIL_WIDTH * 2.0f;
    }

    /// <summary>
	/// X方向の幅とZ方向の奥行きのどちらが長いかを判定する関数
    /// </summary>
    static bool IsDepthLongSide()
    {
        return GetFieldDepth() >= GetFieldWidth();
    }
}