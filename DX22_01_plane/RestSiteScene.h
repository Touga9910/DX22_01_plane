#pragma once

#include "Scene.h"
#include "MenuSelection.h"
#include "LoungeServicePresentation.h"

#include <string>
#include <vector>

class GameObject;

class RestSiteScene : public Scene
{
public:
	RestSiteScene();
	~RestSiteScene() override;

	void Update() override;
	void DrawUI() override;
	/// <summary>
	/// 現在の休憩所で行動を使用済みかを返す。
	/// </summary>
	bool HasUsedAction() const { return m_ActionUsed; }
	/// <summary>
	/// 現在の休憩所で行動を使用済みとして記録する。
	/// </summary>
	void MarkActionUsed() { m_ActionUsed = true; }

private:
	void Init();
	void Uninit();

	std::vector<GameObject*> m_SceneGameObjects;
	MenuSelection m_Menu;
	LoungeServiceUi::State m_UiState;
	int m_SelectedBall = 0;
	bool m_ActionUsed = false;
	std::string m_Message;
};
