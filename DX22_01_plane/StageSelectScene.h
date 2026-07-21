#pragma once
#include "Scene.h"
#include "Component.h"
#include <vector>

// StageSelectSceneクラス
class StageSelectScene : public Scene
{
private:
	std::vector<Component*> m_MySceneObjects; // このシーンが所有するGameObject内の代表Component

	int SelectArrow;
	class Texture2D* m_pArrowImage;

	void Init(); // 初期化
	void Uninit(); // 終了処理

public:
	StageSelectScene(); // コンストラクタ
	~StageSelectScene(); // デストラクタ

	void Update(); // 更新
};

