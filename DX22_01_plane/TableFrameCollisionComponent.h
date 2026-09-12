#pragma once

#include "Collision.h"
#include "Component.h"

#include <vector>

// テーブル枠のローカル座標における衝突壁を所有する。
class TableFrameCollisionComponent final : public Component
{
public:
    void Awake() override;

    static std::vector<Collision::Segment> BuildLocalWalls();

    const std::vector<Collision::Segment>& GetLocalWalls() const
    {
        return m_Walls;
    }

private:
    static void AddWall(
        std::vector<Collision::Segment>& walls,
        const DirectX::SimpleMath::Vector3& start,
        const DirectX::SimpleMath::Vector3& end);

    std::vector<Collision::Segment> m_Walls;
};
