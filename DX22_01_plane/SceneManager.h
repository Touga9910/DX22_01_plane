#pragma once

#include "GameTypes.h"

#include <memory>

class Scene;

// 現在のシーンの所有権と具体的なシーン生成を一元管理する。
class SceneManager final
{
public:
	SceneManager();
	~SceneManager();

	SceneManager(const SceneManager&) = delete;
	SceneManager& operator=(const SceneManager&) = delete;

	// 初期シーンを生成して管理対象に設定する。
	bool Initialize(SceneType sceneType);

	// 有効な遷移先シーンを生成し、現在のシーンと入れ替える。
	bool Change(SceneType sceneType);

	// 現在のシーンを破棄して未初期化状態へ戻す。
	void Reset();

	// 現在のシーンを返す。
	Scene* Get() const { return m_Current.get(); }

	// 現在のシーン種別を返す。
	SceneType GetType() const { return m_CurrentType; }

private:
	// 指定された種別に対応する具体的なシーンを生成する。
	std::unique_ptr<Scene> Create(SceneType sceneType) const;

	std::unique_ptr<Scene> m_Current;
	SceneType m_CurrentType = SceneType::Max;
};
