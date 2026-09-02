#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "BallStatus.h"

struct BallUpgradeStep
{
	int attack = 1;
	int defense = 0;
};

// プレイヤーのデッキに含まれる、ボール1個分の実行時データ。
struct PlayerBallData
{
	static constexpr int MaxUpgradeLevel = 2;

	std::string definitionId = "player_default";
	std::uint64_t instanceId = 0;
	BallStatus status{};
	std::array<BallUpgradeStep, MaxUpgradeLevel> upgradeTable{};
	int upgradeLevel = 0;

	bool CanUpgrade() const
	{
		return upgradeLevel >= 0 && upgradeLevel < MaxUpgradeLevel;
	}
};
