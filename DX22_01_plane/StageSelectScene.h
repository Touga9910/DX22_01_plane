#pragma once
#include "Scene.h"

#include <array>
#include <random>
#include <string>
#include <vector>

class GameObject;

enum class StageRouteType
{
	NormalBattle,
	MidBoss,
	Shop,
	RestSite,
	FinalBoss
};

// StageSelectSceneクラス
class StageSelectScene : public Scene
{
private:
	static constexpr int kNodeCount = 3;

	std::vector<GameObject*> m_SceneGameObjects;

	std::array<StageRouteType, kNodeCount> m_RouteNodes{};
	std::mt19937 m_RandomEngine;
	int m_SelectedNode = 0;

	void Init(); // 初期化
	void Uninit(); // 終了処理
	void RollRouteNodes();

public:
	StageSelectScene(); // コンストラクタ
	~StageSelectScene(); // デストラクタ

	void Update(); // 更新
	void DrawUI() override;
	int GetRouteNodeCount() const;
	const char* GetRouteIdAt(int routeIndex) const;
	const char* GetRouteDisplayNameAt(int routeIndex) const;
	bool ChooseRoute(int routeIndex, const std::string& controllerType);
};

