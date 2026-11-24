#pragma once
#include "Scene.h"
#include <vector>

class GameObject;
class PlayerBall;
//class Arrow;

class StageBase : public Scene
{
protected:
    // 各ステージで共有するオブジェクト管理配列
    std::vector<GameObject*> m_SceneGameObjects;

    // ステージ共通の進行ステート
    //int m_State = 0;
    int m_Par = 4;
    int m_StrokeCount = 0;

    // 便利なヘルパー関数（毎回 dynamic_cast する手間を省く）
    PlayerBall* GetPlayerBall() const;

    // 打数UIの更新処理を共通化
    void UpdateStrokeUI();

public:
    StageBase();
    virtual ~StageBase() override;

    //  Scene::Update をここでオーバーライドし、共通の進行ロジックを書く
    void Update() override;

    // 終了処理も共通化
    void Uninit();

    /// <summary>
    /// 打数から規定打数を引いたスコアを返す。
    /// </summary>
    int GetScore() const { return m_StrokeCount - m_Par; }

    void RemoveInvalidSceneObjectRefs();
};
