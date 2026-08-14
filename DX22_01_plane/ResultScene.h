#pragma once
#include "Scene.h"
#include <vector>

class GameObject;

// ResultSceneクラス
class ResultScene : public Scene
{
private:
	std::vector<GameObject*> m_SceneGameObjects;

	void Init(); // 初期化
	void Uninit(); // 終了処理

public:
	ResultScene(); // コンストラクタ
	~ResultScene(); // デストラクタ

	void Update(); // 更新

	void SetScore(int c);//スコアを設定
};

