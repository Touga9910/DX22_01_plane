#pragma once
#include <memory>

// 各シーンの基底クラス。
class Scene
{
public:

	Scene();
	virtual ~Scene();

	virtual void Update() = 0;
	virtual void DrawUI() {}
};
