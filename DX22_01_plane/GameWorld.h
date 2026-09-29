#pragma once

#include "Component.h"
#include "GameObjectTag.h"

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

class GameObject;

// ゲーム空間に存在するGameObjectの所有権と検索を一元管理
class GameWorld final
{
public:
	GameWorld() = default;
	~GameWorld();

	GameWorld(const GameWorld&) = delete;
	GameWorld& operator=(const GameWorld&) = delete;

	// 名前を指定してGameObjectを生成し、ゲーム空間へ登録
	GameObject* Create(const std::string& name);

	// 指定したGameObjectへ遅延削除要求を送る。
	void RequestDestroy(GameObject* gameObject);

	// 削除要求済みのGameObjectをゲーム空間から取り除く。
	void RemoveDestroyed();

	// 有効なGameObjectのフレーム更新を実行
	void Update();

	// 有効なGameObjectの固定時間更新を実行
	void FixedUpdate();

	// 有効なGameObjectの後処理更新を実行
	void LateUpdate();

	// 有効なGameObjectの描画を実行
	void Draw();

	// すべてのGameObjectを終了処理して破棄
	void Clear();

	// 指定したGameObjectが現在のゲーム空間に存在するかを返す。
	bool Contains(const GameObject* gameObject) const;

	// 指定したComponentが現在のゲーム空間に属しているかを返す。
	bool Contains(const Component* component) const;

	// 指定した型のComponentをゲーム空間全体から取得
	template<typename T>
	std::vector<T*> GetComponents() const
	{
		static_assert(
			std::is_base_of_v<Component, T>,
			"T must inherit from Component");

		std::vector<T*> result;
		VisitComponents(&CollectComponent<T>, &result);
		return result;
	}

	// 指定した型のComponentを持つGameObjectをゲーム空間全体から取得
	template<typename T>
	std::vector<GameObject*> GetObjectsWith() const
	{
		static_assert(
			std::is_base_of_v<Component, T>,
			"T must inherit from Component");

		std::vector<GameObject*> result;
		VisitComponents(&CollectOwner<T>, &result);
		return result;
	}

	// 指定したタグを持つGameObjectをゲーム空間全体から取得
	std::vector<GameObject*> GetObjectsWithTag(GameObjectTag tag) const;

private:
	using ComponentVisitor = void (*)(Component*, void*);

	// Keep GameObject's storage private without allocating an intermediate list.
	void VisitComponents(ComponentVisitor visitor, void* context) const;

	template<typename T>
	static void CollectComponent(Component* component, void* context)
	{
		if (T* typed = dynamic_cast<T*>(component))
		{
			static_cast<std::vector<T*>*>(context)->push_back(typed);
		}
	}

	template<typename T>
	static void CollectOwner(Component* component, void* context)
	{
		if (T* typed = dynamic_cast<T*>(component))
		{
			static_cast<std::vector<GameObject*>*>(context)->push_back(
				typed->GetGameObject());
		}
	}

	std::vector<std::unique_ptr<GameObject>> m_Objects;
};
