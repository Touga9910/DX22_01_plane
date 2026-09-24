#pragma once

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "Component.h"

class TransformComponent;

// 複数のComponentをまとめて管理するゲームオブジェクト。
// 更新・描画・破棄要求を各コンポーネントへ伝え、TransformComponentを常に1つ保持する。
class GameObject
{
public:
    // -------------------------
    // 生成・終了
    // -------------------------

    // 指定した名前でGameObjectを生成し、TransformComponentを自動追加する。
    explicit GameObject(const std::string& name);

    // 終了時に各コンポーネントへOnDestroyを通知する。
    ~GameObject();

    // 各コンポーネントへOnDestroyを通知し、終了処理済み状態へ移行する。
    // すでに終了処理済みの場合は何もしない。
    void Uninit();

    // -------------------------
    // 更新・描画
    // -------------------------

    // 有効なコンポーネントを毎フレーム更新する。
    // 各コンポーネントのStartは最初のUpdateまたはFixedUpdate前に一度だけ呼ばれる。
    void Update();

    // 有効なコンポーネントを固定時間刻みで更新する。
    // 各コンポーネントのStartは最初のUpdateまたはFixedUpdate前に一度だけ呼ばれる。
    void FixedUpdate();

    // 通常のUpdate後に、有効なコンポーネントのLateUpdateを呼び出す。
    void LateUpdate();

    // 有効なコンポーネントのDrawを呼び出す。
    void Draw();

    // -------------------------
    // 破棄状態
    // -------------------------

    // このGameObjectへ破棄要求を設定する。
    // 実際の削除は管理側で行い、要求後は更新・描画処理を行わない。
    void Destroy();

    bool IsDead() const
    {
        return m_DestroyRequested;
    }

    bool IsDestroyRequested() const
    {
        return m_DestroyRequested;
    }

    // -------------------------
    // 名前
    // -------------------------

    const std::string& GetName() const
    {
        return m_Name;
    }

    void SetName(const std::string& name)
    {
        m_Name = name;
    }

    // -------------------------
    // 有効状態
    // -------------------------

    bool IsActive() const
    {
        return m_IsActive;
    }

    // GameObject全体の更新・描画有効状態を設定する。
    // falseの場合、各コンポーネントのUpdate・FixedUpdate・LateUpdate・Drawは実行されない。
    void SetActive(bool active)
    {
        m_IsActive = active;
    }

    // -------------------------
    // Transform取得
    // -------------------------

    // このGameObjectが保持するTransformComponentを返す。
    TransformComponent* GetTransform() const
    {
        return m_Transform;
    }

    // -------------------------
    // コンポーネント追加・取得
    // -------------------------

    // 指定したComponent派生型を追加する。
    // 同じ型のコンポーネントをすでに持っている場合は、新規生成せず既存のものを返す。
    // 追加時に所有元を設定し、登録直後にAwakeを呼び出す。
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

    // 指定したComponent派生型を1つ取得する。
    // 対象型のコンポーネントを持っていない場合はnullptrを返す。
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

    // 指定したComponent派生型を持っている場合はtrueを返す。
    template<class T>
    bool HasComponent() const
    {
        return GetComponent<T>() != nullptr;
    }

    // 指定したComponent派生型に一致するすべてのコンポーネントを取得する。
    // 一致するコンポーネントがない場合は空のvectorを返す。
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
    // -------------------------
    // メンバー変数
    // -------------------------

    std::string m_Name;                                      // GameObjectの識別用名称

    std::vector<std::unique_ptr<Component>> m_Components;    // このGameObjectが所有するコンポーネント一覧

    TransformComponent* m_Transform = nullptr;               // 自動追加されるTransformComponentへの参照

    bool m_IsActive = true;                                  // trueの場合、更新・描画処理の対象となる
    bool m_DestroyRequested = false;                         // trueの場合、破棄要求済みとして更新・描画を停止する
    bool m_IsFinalized = false;                              // Uninitによる終了処理が完了済みか
};
