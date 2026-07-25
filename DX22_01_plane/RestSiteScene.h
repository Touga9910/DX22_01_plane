#pragma once

#include "Scene.h"
#include "Component.h"

#include <string>
#include <vector>

class RestSiteScene : public Scene
{
public:
	RestSiteScene();
	~RestSiteScene() override;

	void Update() override;
	void DrawUI() override;

private:
	void Init();
	void Uninit();

	std::vector<Component*> m_MySceneObjects;
	int m_SelectedAction = 0;
	int m_SelectedBall = 0;
	bool m_ActionUsed = false;
	std::string m_Message;
};
