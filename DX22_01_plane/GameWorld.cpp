#include "GameWorld.h"

#include "GameObject.h"
#include "TagComponent.h"

#include <algorithm>

/// <summary>
/// ワールドが所有するゲームオブジェクトを解放する。
/// </summary>
GameWorld::~GameWorld() = default;

/// <summary>
/// 削除予定のオブジェクトを除き、所有する各コンポーネントに処理を適用する。
/// </summary>
void GameWorld::VisitComponents(
	ComponentVisitor visitor,
	void* context) const
{
	for (const auto& gameObject : m_Objects)
	{
		if (gameObject->IsDestroyRequested())
		{
			continue;
		}
		for (const auto& component : gameObject->m_Components)
		{
			visitor(component.get(), context);
		}
	}
}

/// <summary>
/// 指定した名前でゲームオブジェクトを作成し、ワールドに登録する。
/// </summary>
GameObject* GameWorld::Create(const std::string& name)
{
	auto gameObject = std::make_unique<GameObject>(name);
	GameObject* result = gameObject.get();
	m_Objects.emplace_back(std::move(gameObject));
	return result;
}

/// <summary>
/// ワールドが所有する指定オブジェクトへ遅延削除を要求する。
/// </summary>
void GameWorld::RequestDestroy(GameObject* gameObject)
{
	if (gameObject != nullptr && Contains(gameObject))
	{
		gameObject->Destroy();
	}
}

/// <summary>
/// 削除が要求されたゲームオブジェクトをワールドから取り除く。
/// </summary>
void GameWorld::RemoveDestroyed()
{
	std::erase_if(
		m_Objects,
		[](const std::unique_ptr<GameObject>& gameObject)
		{
			return gameObject->IsDestroyRequested();
		});
}

/// <summary>
/// 開始時点で存在するオブジェクトのフレーム更新を実行する。
/// </summary>
void GameWorld::Update()
{
	// Components may append objects; never retain a vector iterator across callbacks.
	// Defer newly created objects to the next pass of this kind.
	const std::size_t count = m_Objects.size();
	for (std::size_t index = 0; index < count; ++index)
	{
		GameObject* gameObject = m_Objects[index].get();
		gameObject->Update();
	}
}

/// <summary>
/// 開始時点で存在するオブジェクトの固定時間更新を実行する。
/// </summary>
void GameWorld::FixedUpdate()
{
	// Components may append objects; never retain a vector iterator across callbacks.
	// Defer newly created objects to the next pass of this kind.
	const std::size_t count = m_Objects.size();
	for (std::size_t index = 0; index < count; ++index)
	{
		GameObject* gameObject = m_Objects[index].get();
		gameObject->FixedUpdate();
	}
}

/// <summary>
/// 開始時点で存在するオブジェクトの後処理更新を実行する。
/// </summary>
void GameWorld::LateUpdate()
{
	// Components may append objects; never retain a vector iterator across callbacks.
	// Defer newly created objects to the next pass of this kind.
	const std::size_t count = m_Objects.size();
	for (std::size_t index = 0; index < count; ++index)
	{
		GameObject* gameObject = m_Objects[index].get();
		gameObject->LateUpdate();
	}
}

/// <summary>
/// 開始時点で存在するオブジェクトの描画を実行する。
/// </summary>
void GameWorld::Draw()
{
	// Components may append objects; never retain a vector iterator across callbacks.
	// Defer newly created objects to the next pass of this kind.
	const std::size_t count = m_Objects.size();
	for (std::size_t index = 0; index < count; ++index)
	{
		GameObject* gameObject = m_Objects[index].get();
		gameObject->Draw();
	}
}

/// <summary>
/// 全オブジェクトの終了処理を呼び出し、ワールドを空にする。
/// </summary>
void GameWorld::Clear()
{
	for (const auto& gameObject : m_Objects)
	{
		gameObject->Uninit();
	}
	m_Objects.clear();
}

/// <summary>
/// 指定したゲームオブジェクトがワールドに登録され、削除予定になっていないかを返す。
/// </summary>
bool GameWorld::Contains(const GameObject* gameObject) const
{
	if (gameObject == nullptr)
	{
		return false;
	}

	return std::any_of(
		m_Objects.begin(),
		m_Objects.end(),
		[gameObject](const std::unique_ptr<GameObject>& owned)
		{
			return owned.get() == gameObject && !owned->IsDestroyRequested();
		});
}

/// <summary>
/// 指定したコンポーネントのアドレスが、ワールド所有の有効なオブジェクトに属するかを返す。
/// </summary>
bool GameWorld::Contains(const Component* component) const
{
	// Callers may retain an address after its component has been destroyed.
	// Establish ownership without ever dereferencing the supplied address.
	if (component == nullptr) return false;
	for (const auto& gameObject : m_Objects)
	{
		if (gameObject->IsDestroyRequested()) continue;
		for (const auto& owned : gameObject->m_Components)
		{
			if (owned.get() == component) return true;
		}
	}
	return false;
}

/// <summary>
/// 指定したタグが付いた有効なゲームオブジェクトの一覧を返す。
/// </summary>
std::vector<GameObject*> GameWorld::GetObjectsWithTag(GameObjectTag tag) const
{
	std::vector<GameObject*> result;
	for (const auto& gameObject : m_Objects)
	{
		if (gameObject->IsDestroyRequested())
		{
			continue;
		}

		TagComponent* tagComponent = gameObject->GetComponent<TagComponent>();
		if (tagComponent != nullptr && tagComponent->GetTag() == tag)
		{
			result.push_back(gameObject.get());
		}
	}
	return result;
}
