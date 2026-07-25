#pragma once

#include "Scene.h"
#include "Component.h"

#include <string>
#include <vector>

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

	std::vector<Component*> m_MySceneObjects;
	int m_SelectedAction = 0;
	int m_SelectedBuyBall = 0;
	int m_SelectedRemoveBall = 0;
	std::string m_Message;
};
