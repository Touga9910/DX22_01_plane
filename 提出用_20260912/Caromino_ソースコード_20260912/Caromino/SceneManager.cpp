#include "SceneManager.h"

#include "BattleScene.h"
#include "RestSiteScene.h"
#include "ResultScene.h"
#include "Scene.h"
#include "ShopScene.h"
#include "StageSelectScene.h"
#include "TitleScene.h"

// SceneManagerを未初期化状態で生成する。
SceneManager::SceneManager() = default;

// 所有している現在のシーンを安全に破棄する。
SceneManager::~SceneManager() = default;

// 初期シーンを生成して管理対象に設定する。
bool SceneManager::Initialize(SceneType sceneType)
{
	if (m_Current != nullptr)
	{
		return false;
	}
	return Change(sceneType);
}

// 有効な遷移先シーンを生成し、現在のシーンと入れ替える。
bool SceneManager::Change(SceneType sceneType)
{
	if (sceneType < SceneType::Title || sceneType >= SceneType::Max)
	{
		return false;
	}

	// 旧シーンのデストラクタが旧シーン用オブジェクトへ削除要求を送ってから、
	// 新しいシーンのコンストラクタがオブジェクトを生成する順序を維持する。
	m_Current.reset();
	std::unique_ptr<Scene> next = Create(sceneType);
	if (next == nullptr)
	{
		m_CurrentType = SceneType::Max;
		return false;
	}

	m_Current = std::move(next);
	m_CurrentType = sceneType;
	return true;
}

// 現在のシーンを破棄して未初期化状態へ戻す。
void SceneManager::Reset()
{
	m_Current.reset();
	m_CurrentType = SceneType::Max;
}

// 指定された種別に対応する具体的なシーンを生成する。
std::unique_ptr<Scene> SceneManager::Create(SceneType sceneType) const
{
	switch (sceneType)
	{
	case SceneType::Title:
		return std::make_unique<TitleScene>();
	case SceneType::Select:
		return std::make_unique<StageSelectScene>();
	case SceneType::Battle:
		return std::make_unique<BattleScene>();
	case SceneType::RestSite:
		return std::make_unique<RestSiteScene>();
	case SceneType::Shop:
		return std::make_unique<ShopScene>();
	case SceneType::Result:
		return std::make_unique<ResultScene>();
	default:
		return nullptr;
	}
}
