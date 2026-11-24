#include "Game.h"
#include "GameRunSession.h"

#pragma execution_character_set("utf-8")

#include <utility>

/// <summary>
/// 現在のランのセッションがアクティブかを返す。
/// </summary>
bool Game::IsRunSessionActive() const
{
	return m_RunSession->IsActive();
}

/// <summary>
/// 新しいランのセッションを開始して前回の結果を初期化する。
/// </summary>
void Game::BeginRunSession()
{
	m_RunSession->Begin();
}

/// <summary>
/// 現在のランのセッションをアクティブにする。
/// </summary>
void Game::ActivateRunSession()
{
	m_RunSession->Activate();
}

/// <summary>
/// 現在のランのセッションを非アクティブにする。
/// </summary>
void Game::DeactivateRunSession()
{
	m_RunSession->Deactivate();
}

/// <summary>
/// 渡された結果を保存し、現在のランのセッションを終了する。
/// </summary>
void Game::CompleteRunSession(RunResultSnapshot result)
{
	m_RunSession->Complete(std::move(result));
}

/// <summary>
/// 直近のランの結果を参照として返す。
/// </summary>
const RunResultSnapshot& Game::GetLastRunResult() const
{
	return m_RunSession->LastResult();
}
