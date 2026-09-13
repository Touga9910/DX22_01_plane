#include "GameWorld.h"

// 名前を指定してGameObjectを生成し、ゲーム空間へ登録する。
GameObject* GameWorld::Create(const std::string& name)
{
	auto gameObject = std::make_unique<GameObject>(name);
	GameObject* result = gameObject.get();
	m_Objects.emplace_back(std::move(gameObject));
	return result;
}

// 指定したGameObjectへ遅延削除要求を送る。
void GameWorld::RequestDestroy(GameObject* gameObject)
{
	if (gameObject != nullptr && Contains(gameObject))
	{
		gameObject->Destroy();
	}
}

// 削除要求済みのGameObjectをゲーム空間から取り除く。
void GameWorld::RemoveDestroyed()
{
	std::erase_if(
		m_Objects,
		[](const std::unique_ptr<GameObject>& gameObject)
		{
			return gameObject->IsDestroyRequested();
		});
}

// 有効なGameObjectのフレーム更新を実行する。
void GameWorld::Update()
{
	for (const auto& gameObject : m_Objects)
	{
		gameObject->Update();
	}
}

// 有効なGameObjectの固定時間更新を実行する。
void GameWorld::FixedUpdate()
{
	for (const auto& gameObject : m_Objects)
	{
		gameObject->FixedUpdate();
	}
}

// 有効なGameObjectの後処理更新を実行する。
void GameWorld::LateUpdate()
{
	for (const auto& gameObject : m_Objects)
	{
		gameObject->LateUpdate();
	}
}

// 有効なGameObjectの描画を実行する。
void GameWorld::Draw()
{
	for (const auto& gameObject : m_Objects)
	{
		gameObject->Draw();
	}
}

// すべてのGameObjectを終了処理して破棄する。
void GameWorld::Clear()
{
	for (const auto& gameObject : m_Objects)
	{
		gameObject->Uninit();
	}
	m_Objects.clear();
}

// 指定したGameObjectが現在のゲーム空間に存在するかを返す。
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

// 指定したComponentが現在のゲーム空間に属しているかを返す。
bool GameWorld::Contains(const Component* component) const
{
	return component != nullptr && Contains(component->GetGameObject());
}

// 指定したタグを持つGameObjectをゲーム空間全体から取得する。
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
