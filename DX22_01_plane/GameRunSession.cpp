#include "GameRunSession.h"

#include <utility>

/// <summary>
/// 現在ランのセッションが進行中かを返す。
/// </summary>
bool GameRunSession::IsActive() const
{
	return m_Active;
}

/// <summary>
/// セッションを開始し、前回の結果をクリアする。
/// </summary>
void GameRunSession::Begin()
{
	m_Active = true;
	m_LastResult = RunResultSnapshot{};
}

/// <summary>
/// 前回の結果を保持したままセッションを有効にする。
/// </summary>
void GameRunSession::Activate()
{
	m_Active = true;
}

/// <summary>
/// セッションを非アクティブにする。
/// </summary>
void GameRunSession::Deactivate()
{
	m_Active = false;
}

/// <summary>
/// セッションを終了し、渡されたランの結果を保存する。
/// </summary>
void GameRunSession::Complete(RunResultSnapshot result)
{
	m_Active = false;
	m_LastResult = std::move(result);
}

/// <summary>
/// 直近に保存したランの結果を返す。
/// </summary>
const RunResultSnapshot& GameRunSession::LastResult() const
{
	return m_LastResult;
}
