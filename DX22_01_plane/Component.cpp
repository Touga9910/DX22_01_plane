#include "Component.h"

#include "GameObject.h"
#include "TransformComponent.h"

// このコンポーネントが付いているGameObjectを取得する
/// <summary>
/// このコンポーネントを所有するゲームオブジェクトを返す。
/// </summary>
GameObject* Component::GetGameObject() const
{
    return m_Owner;
}

// このコンポーネントが付いているGameObjectのTransformを取得する
/// <summary>
/// 所有オブジェクトからトランスフォームを取得し、未所属なら空のポインターを返す。
/// </summary>
TransformComponent* Component::GetTransform() const
{
    if (m_Owner == nullptr)
    {
        return nullptr;
    }

    return m_Owner->GetTransform();
}