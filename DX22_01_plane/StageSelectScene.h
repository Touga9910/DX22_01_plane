#pragma once
#include "Scene.h"
#include "RunMap.h"

#include <array>
#include <random>
#include <string>
#include <vector>

class GameObject;


// StageSelectSceneクラス
class StageSelectScene : public Scene
{
private:

	std::vector<GameObject*> m_SceneGameObjects;

	std::vector<int> m_RouteNodes;
	bool m_FocusCurrent = true;
	bool m_ShowWholeMap = false;
	int m_SelectedNode = 0;
	bool m_MouseConfirmed = false;

	void Init(); // 初期化
	void Uninit(); // 終了処理


public:
	StageSelectScene(); // コンストラクタ
	~StageSelectScene(); // デストラクタ

	void Update(); // 更新
	void DrawUI() override;
	int GetRouteNodeCount() const;
	int GetMapNodeIdAt(int routeIndex) const;
	const char* GetRouteIdAt(int routeIndex) const;
	const char* GetRouteDisplayNameAt(int routeIndex) const;
	bool ChooseRoute(int routeIndex, const std::string& controllerType);
};

