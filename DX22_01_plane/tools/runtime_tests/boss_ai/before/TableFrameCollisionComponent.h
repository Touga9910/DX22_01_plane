#pragma once

#include "Collision.h"
#include "Component.h"

#include <vector>

// テーブル枠のローカル座標における衝突壁を所有する。
class TableFrameCollisionComponent final : public Component
{
public:
    void Awake() override;

    const std::vector<Collision::Segment>& GetLocalWalls() const
    {
        return m_Walls;
    }

private:
    void AddWall(
        const DirectX::SimpleMath::Vector3& start,
        const DirectX::SimpleMath::Vector3& end);

    std::vector<Collision::Segment> m_Walls;
};
