#pragma once

class GameObject;
class TransformComponent;

// GameObjectへ追加できる各種コンポーネントの基底クラス。
// ライフサイクル処理、所有元GameObjectへの参照、有効状態を共通管理する。
class Component
{
public:
    /// <summary>
    /// コンポーネントの基底部分を既定の状態で構築する。
    /// </summary>
    Component() = default;
    /// <summary>
    /// 基底クラスのポインターを通じて派生コンポーネントを破棄できるようにする。
    /// </summary>
    virtual ~Component() = default;

    // -------------------------
    // ライフサイクル
    // -------------------------

    // GameObjectへ追加された直後に一度呼ばれる。
    /// <summary>
    /// コンポーネントが登録されたときの初期化処理を派生クラスに提供する。
    /// </summary>
    virtual void Awake() {}

    // このコンポーネントが初めてUpdateまたはFixedUpdateされる直前に一度だけ呼ばれる。
    /// <summary>
    /// 初回更新に先立つ開始処理を派生クラスに提供する。
    /// </summary>
    virtual void Start() {}

    // 有効なGameObject上で、毎フレームの通常更新時に呼ばれる。
    /// <summary>
    /// 各フレームで行う更新処理を派生クラスに提供する。
    /// </summary>
    virtual void Update() {}

    // 有効なGameObject上で、固定時間刻みの物理更新時に呼ばれる。
    /// <summary>
    /// 固定時間刻みで行う更新処理を派生クラスに提供する。
    /// </summary>
    virtual void FixedUpdate() {}

    // 通常のUpdate後に呼ばれる。
    /// <summary>
    /// 通常更新後に実行する処理を派生クラスに提供する。
    /// </summary>
    virtual void LateUpdate() {}

    // 描画処理時に呼ばれる。
    /// <summary>
    /// 描画段階で行う処理を派生クラスに提供する。
    /// </summary>
    virtual void Draw() {}

    // 所有するGameObjectの終了処理時に呼ばれる。
    /// <summary>
    /// コンポーネントの破棄前に行う処理を派生クラスに提供する。
    /// </summary>
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

    /// <summary>
    /// このコンポーネントが有効かを返す。
    /// </summary>
    bool IsEnabled() const
    {
        return m_IsEnabled;
    }

    /// <summary>
    /// このコンポーネントを有効または無効にする。
    /// </summary>
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
    /// <summary>
    /// コンポーネントを所有するゲームオブジェクトを設定する。
    /// </summary>
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
