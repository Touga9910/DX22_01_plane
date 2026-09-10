#pragma once

#include "Component.h"
#include "GameObject.h"
#include "TagComponent.h"

#include <algorithm>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

// ゲーム空間に存在するGameObjectの所有権と検索を一元管理する。
class GameWorld final
{
public:
	// 名前を指定してGameObjectを生成し、ゲーム空間へ登録する。
	GameObject* Create(const std::string& name);

	// 指定したGameObjectへ遅延削除要求を送る。
	void RequestDestroy(GameObject* gameObject);

	// 削除要求済みのGameObjectをゲーム空間から取り除く。
	void RemoveDestroyed();

	// 有効なGameObjectのフレーム更新を実行する。
	void Update();

	// 有効なGameObjectの固定時間更新を実行する。
	void FixedUpdate();

	// 有効なGameObjectの後処理更新を実行する。
	void LateUpdate();

	// 有効なGameObjectの描画を実行する。
	void Draw();

	// すべてのGameObjectを終了処理して破棄する。
	void Clear();

	// 指定したGameObjectが現在のゲーム空間に存在するかを返す。
	bool Contains(const GameObject* gameObject) const;

	// 指定したComponentが現在のゲーム空間に属しているかを返す。
	bool Contains(const Component* component) const;

	// 指定した型のComponentをゲーム空間全体から取得する。
	template<typename T>
	std::vector<T*> GetComponents() const
	{
		static_assert(
			std::is_base_of_v<Component, T>,
			"T must inherit from Component");

		std::vector<T*> result;
		for (const auto& gameObject : m_Objects)
		{
			if (gameObject->IsDestroyRequested())
			{
				continue;
			}

			if (T* component = gameObject->GetComponent<T>())
			{
				result.emplace_back(component);
			}
		}
		return result;
	}

	// 指定した型のComponentを持つGameObjectをゲーム空間全体から取得する。
	template<typename T>
	std::vector<GameObject*> GetObjectsWith() const
	{
		static_assert(
			std::is_base_of_v<Component, T>,
			"T must inherit from Component");

		std::vector<GameObject*> result;
		for (const auto& gameObject : m_Objects)
		{
			if (gameObject->IsDestroyRequested())
			{
				continue;
			}

			if (gameObject->HasComponent<T>())
			{
				result.push_back(gameObject.get());
			}
		}
		return result;
	}

	// 指定したタグを持つGameObjectをゲーム空間全体から取得する。
	std::vector<GameObject*> GetObjectsWithTag(GameObjectTag tag) const;

private:
	std::vector<std::unique_ptr<GameObject>> m_Objects;
};
