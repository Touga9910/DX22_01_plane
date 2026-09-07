#pragma once

#include "Scene.h"
#include "MenuSelection.h"

#include <string>
#include <vector>

class GameObject;

class ShopScene : public Scene
{
public:
	ShopScene();
	~ShopScene() override;

	void Update() override;
	void DrawUI() override;

private:
	void Init();
	void Uninit();

	static constexpr int kBallPrice = 20;
	static constexpr int kRemovePrice = 15;

	std::vector<GameObject*> m_SceneGameObjects;
	MenuSelection m_Menu;
	int m_SelectedBuyBall = 0;
	int m_SelectedRemoveBall = 0;
	int m_SelectedRelic = 0;
	std::string m_Message;
};
