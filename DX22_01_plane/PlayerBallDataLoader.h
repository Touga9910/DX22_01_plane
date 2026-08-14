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
	float restHealRatio = 0.25f;
	int restHealCooldownBattles = 2;
	std::vector<PlayerBallData> ballDefinitions;
};

class PlayerBallDataLoader
{
public:
	static PlayerBallDataLoadResult Load(
		const std::string& filePath,
		const BallStatus& fallbackBallStatus,
		const PlayerRunStatus& fallbackRunStatus);

	static std::vector<PlayerBallData> LoadDeck(
		const std::string& filePath,
		const std::vector<PlayerBallData>& ballDefinitions);
};
