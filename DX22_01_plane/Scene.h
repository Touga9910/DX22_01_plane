#pragma once
#include <memory>

// 各シーンの基底クラス。
class Scene
{
public:

	Scene();
	virtual ~Scene();

	/// <summary>
	/// 各シーンのフレームごとの更新処理を派生クラスで定義する。
	/// </summary>
	virtual void Update() = 0;
	/// <summary>
	/// 派生シーンが任意のUI描画を定義する。基底実装の本体は空。
	/// </summary>
	virtual void DrawUI() {}
};
