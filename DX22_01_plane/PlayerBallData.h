#pragma once

#include <string>

#include "BallStatus.h"

// プレイヤーがデッキに持つボール1個分のデータ
struct PlayerBallData
{
	std::string definitionId = "player_default"; // JSONのid
	BallStatus status{};                         // ボールの能力値
};