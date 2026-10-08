#include "GameRunDefaults.h"

BallStatus& GameRunDefaults::PlayerBallStatus()
{
	return m_PlayerBallStatus;
}

const BallStatus& GameRunDefaults::PlayerBallStatus() const
{
	return m_PlayerBallStatus;
}

PlayerRunStatus& GameRunDefaults::PlayerStatus()
{
	return m_PlayerStatus;
}

const PlayerRunStatus& GameRunDefaults::PlayerStatus() const
{
	return m_PlayerStatus;
}

float GameRunDefaults::PlayerRadius() const
{
	return m_PlayerBallStatus.radius;
}
