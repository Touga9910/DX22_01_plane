#pragma once

#include "GameTypes.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// ラン終了時にリザルトシーンへ渡す不変データ。
// このスナップショットの生成後、リザルトUIは変更可能なゲーム状態を参照しない。
struct RunResultSnapshot
{
	bool completed = false;
	int reachedFloor = 1;
	int areaProgress = 0;
	int totalBattles = 0;
	int midBossChallenges = 0;
	int midBossDefeats = 0;
	bool finalBossReached = false;
	bool finalBossDefeated = false;
	std::string finalBossId;
	int totalShots = 0;
	int totalDamage = 0;
	int currentTurnDamage = 0;
	int maximumTurnDamage = 0;
	int damageTaken = 0;
	int currentHp = 0;
	int maxHp = 1;
	std::uint64_t activeFrames = 0;
	std::vector<std::string> acquiredBallIds;
	std::vector<RelicType> acquiredRelics;
	std::unordered_map<std::string, int> ballShotCounts;
};
