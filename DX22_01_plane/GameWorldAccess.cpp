#include "Game.h"
#include "GameRuntimeState.h"
#include "GameWorld.h"

#pragma execution_character_set("utf-8")

GameObject* Game::CreateGameObject(const std::string& name)
{
	return m_RuntimeState->World().Create(name);
}

void Game::DeleteGameObject(GameObject* gameObject)
{
	m_RuntimeState->World().RequestDestroy(gameObject);
}

void Game::RemoveDestroyedGameObjects()
{
	m_RuntimeState->World().RemoveDestroyed();
}

void Game::DeleteAllGameObjects()
{
	m_RuntimeState->World().Clear();
}
