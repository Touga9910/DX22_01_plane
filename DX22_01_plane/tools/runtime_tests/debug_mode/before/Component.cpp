#include "Component.h"

#include "GameObject.h"
#include "TransformComponent.h"

// このコンポーネントが付いているGameObjectを取得する
GameObject* Component::GetGameObject() const
{
    return m_Owner;
}

// このコンポーネントが付いているGameObjectのTransformを取得する
TransformComponent* Component::GetTransform() const
{
    if (m_Owner == nullptr)
    {
        return nullptr;
    }

    return m_Owner->GetTransform();
}