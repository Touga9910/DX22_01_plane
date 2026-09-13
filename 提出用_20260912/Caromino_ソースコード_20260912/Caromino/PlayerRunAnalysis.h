#pragma once

#include "RunResultSnapshot.h"

#include <string>
#include <utility>
#include <vector>

struct PlayerRunAnalysis
{
	float averageDamagePerShot = 0.0f;
	float remainingHpRatio = 0.0f;
	std::vector<std::pair<std::string, int>> ballUsage;
	std::string mostUsedBallId;
	int mostUsedBallCount = 0;
	int acquiredBallCount = 0;
	int acquiredRelicCount = 0;
};

// ファイルアクセス、グローバル状態、描画、値の変更を行わない純粋な分析処理。
PlayerRunAnalysis AnalyzeRun(const RunResultSnapshot& result);
