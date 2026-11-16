#include "Game.h"
#include "GameRunSession.h"

#pragma execution_character_set("utf-8")

#include <utility>

bool Game::IsRunSessionActive() const
{
	return m_RunSession->IsActive();
}

void Game::BeginRunSession()
{
	m_RunSession->Begin();
}

void Game::ActivateRunSession()
{
	m_RunSession->Activate();
}

void Game::DeactivateRunSession()
{
	m_RunSession->Deactivate();
}

void Game::CompleteRunSession(RunResultSnapshot result)
{
	m_RunSession->Complete(std::move(result));
}

const RunResultSnapshot& Game::GetLastRunResult() const
{
	return m_RunSession->LastResult();
}
