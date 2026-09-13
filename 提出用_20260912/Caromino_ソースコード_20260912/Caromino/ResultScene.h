#pragma once
#include "Scene.h"
#include "RunResultSnapshot.h"
#include "PlayerRunAnalysis.h"
#include "MenuSelection.h"
#include <vector>

class GameObject;

// ResultSceneクラス
class ResultScene : public Scene
{
private:
	std::vector<GameObject*> m_SceneGameObjects;
	MenuSelection m_Menu;
	bool m_ShowAnalysis = false;
	RunResultSnapshot m_Result{};
	PlayerRunAnalysis m_Analysis{};

	void Init(); // 初期化
	void Uninit(); // 終了処理
	void DrawSummary();
	void DrawAnalysis();

public:
	ResultScene(); // コンストラクタ
	~ResultScene(); // デストラクタ

	void Update(); // 更新
	void DrawUI() override;

	void SetScore(int c);//スコアを設定
};

