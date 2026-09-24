#pragma once

class GameObject;
class TransformComponent;

// GameObjectへ追加できる各種コンポーネントの基底クラス。
// ライフサイクル処理、所有元GameObjectへの参照、有効状態を共通管理する。
class Component
{
public:
    Component() = default;
    virtual ~Component() = default;

    // -------------------------
    // ライフサイクル
    // -------------------------

    // GameObjectへ追加された直後に一度呼ばれる。
    virtual void Awake() {}

    // このコンポーネントが初めてUpdateまたはFixedUpdateされる直前に一度だけ呼ばれる。
    virtual void Start() {}

    // 有効なGameObject上で、毎フレームの通常更新時に呼ばれる。
    virtual void Update() {}

    // 有効なGameObject上で、固定時間刻みの物理更新時に呼ばれる。
    virtual void FixedUpdate() {}

    // 通常のUpdate後に呼ばれる。
    virtual void LateUpdate() {}

    // 描画処理時に呼ばれる。
    virtual void Draw() {}

    // 所有するGameObjectの終了処理時に呼ばれる。
    virtual void OnDestroy() {}

    // -------------------------
    // 所有オブジェクトの取得
    // -------------------------

    // このコンポーネントを所有するGameObjectを返す。
    // 所有元が設定されていない場合はnullptrを返す。
    GameObject* GetGameObject() const;

    // 所有するGameObjectのTransformComponentを返す。
    // 所有元が設定されていない場合はnullptrを返す。
    TransformComponent* GetTransform() const;

    // -------------------------
    // 有効状態
    // -------------------------

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

    // -------------------------
    // 所有元設定
    // -------------------------

    // このコンポーネントを所有するGameObjectを設定する。
    // GameObject::AddComponentからコンポーネント追加時に呼ばれる。
    void SetOwner(GameObject* owner)
    {
        m_Owner = owner;
    }

private:
    // -------------------------
    // メンバー変数
    // -------------------------

    GameObject* m_Owner = nullptr;   // このコンポーネントを所有するGameObject。未設定時はnullptr

    bool m_IsEnabled = true;         // trueの場合、GameObjectから更新・描画処理の対象となる
    bool m_HasStarted = false;       // Startを実行済みか。falseの場合、最初のUpdateまたはFixedUpdate前にStartを呼ぶ
};
