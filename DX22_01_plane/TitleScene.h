#pragma once
#include "Scene.h"
#include "Component.h"
#include <vector>

// TitleSceneクラス
class TitleScene : public Scene
{
private:
	std::vector<Component*> m_MySceneObjects; // このシーンが所有するGameObject内の代表Component

	int m_CursolPos = 0;	//カーソルの位置

	void Init(); // 初期化
	void Uninit(); // 終了処理

public:
	TitleScene(); // コンストラクタ
	~TitleScene(); // デストラクタ

	void Update(); // 更新
};

