#pragma once

#include "Component.h"

enum class GameObjectTag
{
    None,
    Player,
    Enemy,
    Ground,
    Rail,
    Pocket,
    Goal,
    WorldUi,
    ScreenUi,
};

class TagComponent final : public Component
{
public:
    explicit TagComponent(GameObjectTag tag = GameObjectTag::None)
        : m_Tag(tag)
    {
    }

    GameObjectTag GetTag() const { return m_Tag; }
    void SetTag(GameObjectTag tag) { m_Tag = tag; }

private:
    GameObjectTag m_Tag = GameObjectTag::None;
};
