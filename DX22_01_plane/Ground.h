#pragma once

#include "Component.h"
#include "Renderer.h"
#include "TableConfig.h"

#include <vector>

class GroundRenderComponent;

// 地面固有の設定と、ゲームプレイ用の公開APIを扱う。
// 描画は同じGameObjectのGroundRenderComponentが担当する。
class Ground final : public Component
{
public:
    void Awake() override;

    std::vector<VERTEX_3D> GetVertices();

    /// <summary>
    /// 盤面の表面の高さを返す。
    /// </summary>
    float GetFieldHeight() const { return TableConfig::FIELD_HEIGHT; }
    /// <summary>
    /// 盤面の幅を返す。
    /// </summary>
    float GetFieldWidth() const { return TableConfig::GetFieldWidth(); }
    /// <summary>
    /// 盤面の奥行きを返す。
    /// </summary>
    float GetFieldDepth() const { return TableConfig::GetFieldDepth(); }

private:
    GroundRenderComponent* m_RenderComponent = nullptr;
};
