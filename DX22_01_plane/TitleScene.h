#pragma once
#include "Scene.h"
#include "MenuSelection.h"
#include <string>
#include <vector>

class GameObject;

// TitleSceneクラス
class TitleScene : public Scene
{
private:
	std::vector<GameObject*> m_SceneGameObjects;

	MenuSelection m_Menu;
	bool m_CanContinue = false;
	bool m_ConfirmNewRun = false;
	std::string m_SaveSummary;
	std::string m_Message;

	void Init(); // 初期化
	void Uninit(); // 終了処理

public:
	TitleScene(); // コンストラクタ
	~TitleScene(); // デストラクタ

	void Update(); // 更新
	void DrawUI() override;
};

