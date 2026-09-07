#pragma once

class GameObject;
class TransformComponent;

class Component
{
public:
    Component() = default;
    virtual ~Component() = default;

    // コンポーネントがGameObjectへ追加された直後に呼ばれる
    virtual void Awake() {}

    // 最初のUpdate直前に一度だけ呼ばれる
    virtual void Start() {}

    // 毎フレーム呼ばれる
    virtual void Update() {}

    // 固定時間刻みの物理更新で呼ばれる
    virtual void FixedUpdate() {}

    // 通常のUpdate後に呼ばれる
    virtual void LateUpdate() {}

    // 描画時に呼ばれる
    virtual void Draw() {}

    // GameObjectが削除される直前に呼ばれる
    virtual void OnDestroy() {}

    GameObject* GetGameObject() const;
    TransformComponent* GetTransform() const;

    bool IsEnabled() const
    {
        return m_IsEnabled;
    }

    void SetEnabled(bool enabled)
    {
        m_IsEnabled = enabled;
    }

private:
    friend class GameObject;

    void SetOwner(GameObject* owner)
    {
        m_Owner = owner;
    }

private:
    GameObject* m_Owner = nullptr;

    bool m_IsEnabled = true;
    bool m_HasStarted = false;
};
