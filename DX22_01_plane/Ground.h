#pragma once

#include "Component.h"
#include "Renderer.h"
#include "TableConfig.h"

#include <vector>

class GroundRenderComponent;

// Ground-specific configuration and public gameplay API.
// Rendering is provided by GroundRenderComponent on the same GameObject.
class Ground final : public Component
{
public:
    void Awake() override;

    std::vector<VERTEX_3D> GetVertices();

    float GetFieldHeight() const { return TableConfig::FIELD_HEIGHT; }
    float GetFieldWidth() const { return TableConfig::GetFieldWidth(); }
    float GetFieldDepth() const { return TableConfig::GetFieldDepth(); }

private:
    GroundRenderComponent* m_RenderComponent = nullptr;
};
