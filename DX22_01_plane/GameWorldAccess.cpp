#include "Game.h"
#include "GameRuntimeState.h"
#include "GameWorld.h"

#pragma execution_character_set("utf-8")

/// <summary>
/// ゲームのワールドに指定名のゲームオブジェクトを作成する。
/// </summary>
GameObject* Game::CreateGameObject(const std::string& name)
{
	return m_RuntimeState->World().Create(name);
}

/// <summary>
/// ゲームのワールドに指定したオブジェクトの遅延削除を要求する。
/// </summary>
void Game::DeleteGameObject(GameObject* gameObject)
{
	m_RuntimeState->World().RequestDestroy(gameObject);
}

/// <summary>
/// 削除が要求されたゲームオブジェクトをワールドから取り除く。
/// </summary>
void Game::RemoveDestroyedGameObjects()
{
	m_RuntimeState->World().RemoveDestroyed();
}

/// <summary>
/// ゲームのワールドで全オブジェクトの終了処理と削除を行う。
/// </summary>
void Game::DeleteAllGameObjects()
{
	m_RuntimeState->World().Clear();
}
