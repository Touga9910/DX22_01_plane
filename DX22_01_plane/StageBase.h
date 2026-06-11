#pragma once
#include "Scene.h"
#include <vector>

class Object; // 前方宣言
class PlayerBall;
class Arrow;

class StageBase : public Scene
{
protected:
    // 各ステージで共有するオブジェクト管理配列
    std::vector<Object*> m_MySceneObjects;

    // ステージ共通の進行ステート
    int m_State = 0;
    int m_Par = 4;
    int m_StrokeCount = 0;

    // 便利なヘルパー関数（毎回 dynamic_cast する手間を省く）
    PlayerBall* GetPlayerBall() const;
    Arrow* GetArrow() const;

    // 打数UIの更新処理を共通化
    void UpdateStrokeUI();

public:
    StageBase();
    virtual ~StageBase() override;

    // ★重要: Scene::Update をここでオーバーライドし、共通の進行ロジックを書く
    void Update() override;

    // 終了処理も共通化
    void Uninit();

    int GetScore() const { return m_StrokeCount - m_Par; }
};