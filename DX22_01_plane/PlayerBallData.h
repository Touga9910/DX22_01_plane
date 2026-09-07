#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "BallStatus.h"

// 各段階のボール性能を丸ごと保持する。
using BallUpgradeStep = BallStatus;

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
