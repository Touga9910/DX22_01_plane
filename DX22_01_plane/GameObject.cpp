#include "GameObject.h"

#include "TransformComponent.h"

// GameObject生成時にTransformComponentを自動追加する
GameObject::GameObject(const std::string& name)
    : m_Name(name)
{
    m_Transform = AddComponent<TransformComponent>();
}

// 各コンポーネントに削除通知を送る
GameObject::~GameObject()
{
	Uninit();
}

void GameObject::Uninit()
{
	if (m_IsFinalized)
	{
		return;
	}

    for (const auto& component : m_Components)
    {
        component->OnDestroy();
    }

	m_IsFinalized = true;
}

// 有効なコンポーネントを毎フレーム更新する
void GameObject::Update()
{
    if (!m_IsActive)
    {
        return;
    }

    for (const auto& component : m_Components)
    {
        if (!component->IsEnabled())
        {
            continue;
        }

        // Startは最初のUpdate前に一度だけ実行する
        if (!component->m_HasStarted)
        {
            component->Start();
            component->m_HasStarted = true;
        }

        component->Update();
    }
}

// 通常のUpdate後にコンポーネントを更新する
void GameObject::LateUpdate()
{
    if (!m_IsActive)
    {
        return;
    }

    for (const auto& component : m_Components)
    {
        if (component->IsEnabled())
        {
            component->LateUpdate();
        }
    }
}

// 描画機能を持つコンポーネントを実行する
void GameObject::Draw()
{
    if (!m_IsActive)
    {
        return;
    }

    for (const auto& component : m_Components)
    {
        if (component->IsEnabled())
        {
            component->Draw();
        }
    }
}

// Sceneへ削除要求を送る
void GameObject::Destroy()
{
    m_DestroyRequested = true;
}

void GameObject::FixedUpdate()
{
	if (!m_IsActive)
	{
		return;
	}

	for (const auto& component : m_Components)
	{
		if (component->IsEnabled())
		{
			component->FixedUpdate();
		}
	}
}
