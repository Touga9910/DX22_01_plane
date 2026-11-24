#include "GameRunDefaults.h"

/// <summary>
/// 新しいランで使うプレイヤー状態に初期所持金を設定する。
/// </summary>
GameRunDefaults::GameRunDefaults()
{
	m_PlayerStatus.money = InitialMoney;
}

/// <summary>
/// 初期プレイヤー球の変更可能なパラメーターを返す。
/// </summary>
BallStatus& GameRunDefaults::PlayerBallStatus()
{
	return m_PlayerBallStatus;
}

/// <summary>
/// 初期プレイヤー球のパラメーターを参照として返す。
/// </summary>
const BallStatus& GameRunDefaults::PlayerBallStatus() const
{
	return m_PlayerBallStatus;
}

/// <summary>
/// ラン開始時のプレイヤー状態を変更可能な参照として返す。
/// </summary>
PlayerRunStatus& GameRunDefaults::PlayerStatus()
{
	return m_PlayerStatus;
}

/// <summary>
/// ラン開始時のプレイヤー状態を参照として返す。
/// </summary>
const PlayerRunStatus& GameRunDefaults::PlayerStatus() const
{
	return m_PlayerStatus;
}

/// <summary>
/// 初期プレイヤー球の半径を返す。
/// </summary>
float GameRunDefaults::PlayerRadius() const
{
	return m_PlayerBallStatus.radius;
}
