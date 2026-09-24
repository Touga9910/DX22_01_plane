#pragma once

#include "SimpleMath.h"

#include <array>

// ビリヤード台の寸法とポケット配置に関する共通設定をまとめる
namespace TableConfig
{
    // -------------------------
    // 台・レール・ポケット寸法
    // -------------------------

    // プレイ可能なクロス部分が標準的な2:1比率になるように外寸を設定
    static constexpr float TABLE_OUTER_WIDTH = 152.0f;              // 台全体のX方向幅
    static constexpr float TABLE_OUTER_DEPTH = 80.0f;               // 台全体のZ方向奥行き
    static constexpr float RAIL_WIDTH = 4.0f;                       // レールの幅
    static constexpr float POCKET_RADIUS = 3.0f;                    // ポケット判定半径
    static constexpr float CORNER_POCKET_MOUTH_HALF_WIDTH = 3.5f;   // コーナーポケット開口部の半幅
    static constexpr float SIDE_POCKET_MOUTH_HALF_WIDTH = 3.75f;    // サイドポケット開口部の半幅
    static constexpr float FIELD_HEIGHT = 1.0f;                     // プレイフィールドの基準Y座標
    static constexpr float RAIL_TOP_OFFSET = 0.05f;                 // レール上面の高さ補正値

    // -------------------------
    // プレイ領域
    // -------------------------

    // 左右のレール幅を除いたプレイ可能領域のX方向幅を返す
    inline float GetFieldWidth()
    {
        return TABLE_OUTER_WIDTH - RAIL_WIDTH * 2.0f;
    }

    // 前後のレール幅を除いたプレイ可能領域のZ方向奥行きを返す
    inline float GetFieldDepth()
    {
        return TABLE_OUTER_DEPTH - RAIL_WIDTH * 2.0f;
    }

    // 6個のポケット中心座標を返す
    // 長辺側の両端と中央に3個ずつ配置
    inline std::array<DirectX::SimpleMath::Vector3, 6>
        GetPocketCenters()
    {
        const float halfWidth = GetFieldWidth() * 0.5f;
        const float halfDepth = GetFieldDepth() * 0.5f;
        const float y = 0.05f;
        return {
            DirectX::SimpleMath::Vector3(-halfWidth, y, halfDepth),
            DirectX::SimpleMath::Vector3(0.0f, y, halfDepth),
            DirectX::SimpleMath::Vector3(halfWidth, y, halfDepth),
            DirectX::SimpleMath::Vector3(-halfWidth, y, -halfDepth),
            DirectX::SimpleMath::Vector3(0.0f, y, -halfDepth),
            DirectX::SimpleMath::Vector3(halfWidth, y, -halfDepth),
        };
    }

    // プレイ領域でZ方向がX方向以上の長さか判定する
    inline bool IsDepthLongSide()
    {
        return GetFieldDepth() >= GetFieldWidth();
    }
}

// レールを除いたプレイ領域が2:1比率を維持していることをコンパイル時に確認
static_assert(
    TableConfig::TABLE_OUTER_WIDTH - TableConfig::RAIL_WIDTH * 2.0f ==
    (TableConfig::TABLE_OUTER_DEPTH - TableConfig::RAIL_WIDTH * 2.0f) * 2.0f,
    "The billiard playing surface must keep a 2:1 aspect ratio.");
