#pragma once

#include "BallStatus.h"
#include "PlayerRunStatus.h"

// Owns the configuration-derived defaults used when a run is created. These
// values are separate from the mutable RunController state of an active run.
class GameRunDefaults final
{
public:
	BallStatus& PlayerBallStatus();
	const BallStatus& PlayerBallStatus() const;
	PlayerRunStatus& PlayerStatus();
	const PlayerRunStatus& PlayerStatus() const;
	float PlayerRadius() const;

private:
	BallStatus m_PlayerBallStatus{};
	PlayerRunStatus m_PlayerStatus{};
};
