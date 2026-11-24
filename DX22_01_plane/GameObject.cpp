#include "GameObject.h"

#include "TransformComponent.h"

// GameObject生成時にTransformComponentを自動追加する
/// <summary>
/// 指定名でゲームオブジェクトを作り、位置や姿勢を管理するコンポーネントを追加する。
/// </summary>
GameObject::GameObject(const std::string& name)
    : m_Name(name)
{
    m_Transform = AddComponent<TransformComponent>();
}

// 各コンポーネントに削除通知を送る
/// <summary>
/// 破棄時にコンポーネントの終了処理を呼び出す。
/// </summary>
GameObject::~GameObject()
{
	Uninit();
}

/// <summary>
/// 登録されているコンポーネントを終了させ、二重に終了処理が走らないようにする。
/// </summary>
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
/// <summary>
/// 有効なコンポーネントを必要に応じて開始した後、フレーム更新を呼び出す。
/// </summary>
void GameObject::Update()
{
    if (!m_IsActive || m_DestroyRequested)
    {
        return;
    }

    // Callbacks may append components; defer new entries to the next pass.
    const std::size_t count = m_Components.size();
    for (std::size_t index = 0; index < count; ++index)
    {
        Component* component = m_Components[index].get();
        if (m_DestroyRequested)
        {
            break;
        }

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
/// <summary>
/// 有効なコンポーネントのフレーム更新後の処理を呼び出す。
/// </summary>
void GameObject::LateUpdate()
{
    if (!m_IsActive || m_DestroyRequested)
    {
        return;
    }

    // Callbacks may append components; defer new entries to the next pass.
    const std::size_t count = m_Components.size();
    for (std::size_t index = 0; index < count; ++index)
    {
        Component* component = m_Components[index].get();
        if (m_DestroyRequested)
        {
            break;
        }

        if (component->IsEnabled())
        {
            component->LateUpdate();
        }
    }
}

// 描画機能を持つコンポーネントを実行する
/// <summary>
/// 有効なコンポーネントの描画処理を呼び出す。
/// </summary>
void GameObject::Draw()
{
    if (!m_IsActive || m_DestroyRequested)
    {
        return;
    }

    // Callbacks may append components; defer new entries to the next pass.
    const std::size_t count = m_Components.size();
    for (std::size_t index = 0; index < count; ++index)
    {
        Component* component = m_Components[index].get();
        if (m_DestroyRequested)
        {
            break;
        }

        if (component->IsEnabled())
        {
            component->Draw();
        }
    }
}

// Sceneへ削除要求を送る
/// <summary>
/// ゲームオブジェクトを後続の処理で破棄するように予約する。
/// </summary>
void GameObject::Destroy()
{
    m_DestroyRequested = true;
}

/// <summary>
/// 有効なコンポーネントを必要に応じて開始した後、固定時間ステップの更新を呼び出す。
/// </summary>
void GameObject::FixedUpdate()
{
	if (!m_IsActive || m_DestroyRequested)
	{
		return;
	}

	// Callbacks may append components; defer new entries to the next pass.
	const std::size_t count = m_Components.size();
	for (std::size_t index = 0; index < count; ++index)
	{
	    Component* component = m_Components[index].get();
		if (m_DestroyRequested)
		{
			break;
		}

		if (component->IsEnabled())
		{
			if (!component->m_HasStarted)
			{
				component->Start();
				component->m_HasStarted = true;
			}
			component->FixedUpdate();
		}
	}
}
