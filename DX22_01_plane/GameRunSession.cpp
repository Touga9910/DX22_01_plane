#include "GameRunSession.h"

#include <utility>

bool GameRunSession::IsActive() const
{
	return m_Active;
}

void GameRunSession::Begin()
{
	m_Active = true;
	m_LastResult = RunResultSnapshot{};
}

void GameRunSession::Activate()
{
	m_Active = true;
}

void GameRunSession::Deactivate()
{
	m_Active = false;
}

void GameRunSession::Complete(RunResultSnapshot result)
{
	m_Active = false;
	m_LastResult = std::move(result);
}

const RunResultSnapshot& GameRunSession::LastResult() const
{
	return m_LastResult;
}
