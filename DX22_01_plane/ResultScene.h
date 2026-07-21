#pragma once
#include "Scene.h"
#include "Component.h"
#include <vector>

// ResultSceneクラス
class ResultScene : public Scene
{
private:
	std::vector<Component*> m_MySceneObjects; // このシーンが所有するGameObject内の代表Component

	void Init(); // 初期化
	void Uninit(); // 終了処理

public:
	ResultScene(); // コンストラクタ
	~ResultScene(); // デストラクタ

	void Update(); // 更新

	void SetScore(int c);//スコアを設定
};

