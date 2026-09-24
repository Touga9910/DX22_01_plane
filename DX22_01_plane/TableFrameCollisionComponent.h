#pragma once

#include "Collision.h"
#include "Component.h"

#include <vector>

// テーブル枠のローカル座標における衝突壁を所有
// ポケット開口部を空けたクッション面と、ポケット奥の脱出防止壁を線分として管理
class TableFrameCollisionComponent final : public Component
{
public:
    // TableConfigを基にローカル衝突壁を生成して保持
    void Awake() override;

    // 現在のテーブル寸法・ポケット開口幅から衝突壁線分一覧を生成して返す
    static std::vector<Collision::Segment> BuildLocalWalls();

    // 生成済みのローカル衝突壁一覧を返す
    const std::vector<Collision::Segment>& GetLocalWalls() const
    {
        return m_Walls;
    }

private:
    // startとendから有効な長さを持つ壁線分だけをwallsへ追加
    static void AddWall(
        std::vector<Collision::Segment>& walls,
        const DirectX::SimpleMath::Vector3& start,
        const DirectX::SimpleMath::Vector3& end);

    std::vector<Collision::Segment> m_Walls; // テーブルローカル座標で保持する衝突壁一覧
};
