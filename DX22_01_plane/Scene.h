#pragma once
#include <memory>

// Base scene class.
class Scene
{
public:

	Scene();
	virtual ~Scene();

	virtual void Update() = 0;
	virtual void DrawUI() {}
};
