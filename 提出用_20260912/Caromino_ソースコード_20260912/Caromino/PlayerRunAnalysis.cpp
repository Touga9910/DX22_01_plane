#include "PlayerRunAnalysis.h"

#include <algorithm>

PlayerRunAnalysis AnalyzeRun(const RunResultSnapshot& result)
{
	PlayerRunAnalysis analysis;
	analysis.averageDamagePerShot = result.totalShots > 0
		? static_cast<float>(result.totalDamage) /
			static_cast<float>(result.totalShots)
		: 0.0f;
	analysis.remainingHpRatio = result.maxHp > 0
		? std::clamp(
			static_cast<float>(result.currentHp) /
				static_cast<float>(result.maxHp),
			0.0f,
			1.0f)
		: 0.0f;
	analysis.ballUsage.assign(
		result.ballShotCounts.begin(),
		result.ballShotCounts.end());
	std::sort(
		analysis.ballUsage.begin(),
		analysis.ballUsage.end(),
		[](const auto& left, const auto& right)
		{
			if (left.second != right.second)
			{
				return left.second > right.second;
			}
			return left.first < right.first;
		});
	if (!analysis.ballUsage.empty())
	{
		analysis.mostUsedBallId = analysis.ballUsage.front().first;
		analysis.mostUsedBallCount = analysis.ballUsage.front().second;
	}
	analysis.acquiredBallCount =
		static_cast<int>(result.acquiredBallIds.size());
	analysis.acquiredRelicCount =
		static_cast<int>(result.acquiredRelics.size());
	return analysis;
}
