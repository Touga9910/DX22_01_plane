#pragma once

#include "Collision.h"
#include "Component.h"
#include "TableConfig.h"

#include <vector>

class TableFrameCollisionComponent;
class TableFrameRenderComponent;

// テーブル枠がゲームプレイ向けに公開するAPI。
// 形状と描画は、同じGameObjectにある別コンポーネントが提供する。
class TableFrame final : public Component
{
public:
    void Awake() override;

    std::vector<Collision::Segment> GetWalls() const;
    std::vector<Collision::Sphere> GetPocketSpheres() const;

    float GetOuterWidth() const { return TableConfig::TABLE_OUTER_WIDTH; }
    float GetOuterDepth() const { return TableConfig::TABLE_OUTER_DEPTH; }
    float GetFieldWidth() const { return TableConfig::GetFieldWidth(); }
    float GetFieldDepth() const { return TableConfig::GetFieldDepth(); }
    float GetFieldHeight() const { return TableConfig::FIELD_HEIGHT; }
    float GetRailWidth() const { return TableConfig::RAIL_WIDTH; }
    float GetPocketRadius() const { return TableConfig::POCKET_RADIUS; }

private:
    TableFrameCollisionComponent* m_CollisionComponent = nullptr;
    TableFrameRenderComponent* m_RenderComponent = nullptr;
};
