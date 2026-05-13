#pragma once
#include "Scene.h"
#include "Object.h"

// StageSelectSceneクラス
class StageSelectScene : public Scene
{
private:
	std::vector<Object*> m_MySceneObjects; // このシーンのオブジェクト

	int SelectArrow;
	class Texture2D* m_pArrowImage;

	void Init(); // 初期化
	void Uninit(); // 終了処理

public:
	StageSelectScene(); // コンストラクタ
	~StageSelectScene(); // デストラクタ

	void Update(); // 更新
};

