#pragma once

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "Component.h"

class TransformComponent;

class GameObject
{
public:
    explicit GameObject(const std::string& name);
    ~GameObject();

    void Update();
    void FixedUpdate();
    void LateUpdate();
    void Draw();

    void Destroy();

    void Uninit();

    bool IsDead() const
    {
        return m_DestroyRequested;
    }

    const std::string& GetName() const
    {
        return m_Name;
    }

    void SetName(const std::string& name)
    {
        m_Name = name;
    }

    bool IsActive() const
    {
        return m_IsActive;
    }

    void SetActive(bool active)
    {
        m_IsActive = active;
    }

    bool IsDestroyRequested() const
    {
        return m_DestroyRequested;
    }

    TransformComponent* GetTransform() const
    {
        return m_Transform;
    }

    // 指定した型のコンポーネントを追加する
    template<class T, class... Args>
    T* AddComponent(Args&&... args)
    {
        static_assert(
            std::is_base_of_v<Component, T>,
            "T must inherit from Component"
            );

        if (T* existing = GetComponent<T>())
        {
            return existing;
        }

        auto component =
            std::make_unique<T>(std::forward<Args>(args)...);

        T* componentPointer = component.get();

        componentPointer->SetOwner(this);

        m_Components.push_back(std::move(component));

        componentPointer->Awake();

        return componentPointer;
    }

    // 指定した型のコンポーネントを取得する
    template<class T>
    T* GetComponent() const
    {
        static_assert(
            std::is_base_of_v<Component, T>,
            "T must inherit from Component"
            );

        for (const auto& component : m_Components)
        {
            T* result = dynamic_cast<T*>(component.get());

            if (result != nullptr)
            {
                return result;
            }
        }

        return nullptr;
    }

    // 指定した型のコンポーネントを持っているか調べる
    template<class T>
    bool HasComponent() const
    {
        return GetComponent<T>() != nullptr;
    }

    template<class T>
    std::vector<T*> GetComponents() const
    {
        static_assert(
            std::is_base_of_v<Component, T>,
            "T must inherit from Component"
            );

        std::vector<T*> result;

        for (const auto& component : m_Components)
        {
            if (T* typedComponent = dynamic_cast<T*>(component.get()))
            {
                result.push_back(typedComponent);
            }
        }

        return result;
    }

private:
    std::string m_Name;

    std::vector<std::unique_ptr<Component>> m_Components;

    TransformComponent* m_Transform = nullptr;

    bool m_IsActive = true;
    bool m_DestroyRequested = false;
    bool m_IsFinalized = false;
};
