#include "SceneManager.h"

#include "RestSiteScene.h"
#include "ResultScene.h"
#include "RunEventScene.h"
#include "Scene.h"
#include "ShopScene.h"
#include "StageSelectScene.h"
#include "TitleScene.h"

#include <utility>

// SceneManagerを未初期化状態で生成する。
/// <summary>
/// 管理対象のシーンがない状態でシーン管理を初期化する。
/// </summary>
SceneManager::SceneManager() = default;

// 所有している現在のシーンを安全に破棄する。
/// <summary>
/// 管理対象のシーンを含むシーン管理のリソースを破棄する。
/// </summary>
SceneManager::~SceneManager() = default;

/// <summary>
/// 戦闘シーンを生成するときに使うファクトリーを登録する。
/// </summary>
void SceneManager::SetBattleSceneFactory(BattleSceneFactory factory)
{
	m_BattleSceneFactory = std::move(factory);
}

// 初期シーンを生成して管理対象に設定する。
/// <summary>
/// 未初期化のシーン管理に最初のシーンを設定する。
/// </summary>
bool SceneManager::Initialize(SceneType sceneType)
{
	if (m_Current != nullptr)
	{
		return false;
	}
	return Change(sceneType);
}

// 有効な遷移先シーンを生成し、現在のシーンと入れ替える。
/// <summary>
/// 遷移先の種別を検証し、現在のシーンを破棄して対象のシーンを生成する。
/// </summary>
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
/// <summary>
/// 管理中のシーンを破棄し、シーン種別を未初期化の状態へ戻す。
/// </summary>
void SceneManager::Reset()
{
	m_Current.reset();
	m_CurrentType = SceneType::Max;
}

// 指定された種別に対応する具体的なシーンを生成する。
/// <summary>
/// シーン種別に対応するクラスまたは登録済みのファクトリーからシーンを構築する。
/// </summary>
std::unique_ptr<Scene> SceneManager::Create(SceneType sceneType) const
{
	switch (sceneType)
	{
	case SceneType::Title:
		return std::make_unique<TitleScene>();
	case SceneType::Select:
		return std::make_unique<StageSelectScene>();
	case SceneType::Battle:
		return m_BattleSceneFactory
			? m_BattleSceneFactory()
			: nullptr;
	case SceneType::RestSite:
		return std::make_unique<RestSiteScene>();
	case SceneType::Shop:
		return std::make_unique<ShopScene>();
	case SceneType::Event:
		return std::make_unique<RunEventScene>();
	case SceneType::Result:
		return std::make_unique<ResultScene>();
	default:
		return nullptr;
	}
}
