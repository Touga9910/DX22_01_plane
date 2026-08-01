#pragma once
#include "Scene.h"
#include "Component.h"

#include <array>
#include <random>
#include <vector>

enum class StageRouteType
{
	Battle,
	Shop,
	RestSite
};

// StageSelectSceneクラス
class StageSelectScene : public Scene
{
private:
	static constexpr int kNodeCount = 3;

	std::vector<Component*> m_MySceneObjects; // このシーンが所有するGameObject内の代表Component

	std::array<StageRouteType, kNodeCount> m_RouteNodes{};
	std::mt19937 m_RandomEngine;
	int m_SelectedNode = 0;

	void Init(); // 初期化
	void Uninit(); // 終了処理
	void RollRouteNodes();
	void EnterRoute(StageRouteType routeType);

public:
	StageSelectScene(); // コンストラクタ
	~StageSelectScene(); // デストラクタ

	void Update(); // 更新
	void DrawUI() override;
};

