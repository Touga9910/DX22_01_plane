#pragma once

#include <string>
#include <vector>

#include "BallStatus.h"
#include "PlayerBallData.h"
#include "PlayerRunStatus.h"

struct PlayerBallDataLoadResult
{
	BallStatus defaultBallStatus{};
	PlayerRunStatus defaultRunStatus{};
	std::vector<PlayerBallData> defaultDeck;
};

class PlayerBallDataLoader
{
public:
	static PlayerBallDataLoadResult Load(
		const std::string& filePath,
		const BallStatus& fallbackBallStatus,
		const PlayerRunStatus& fallbackRunStatus);
};
