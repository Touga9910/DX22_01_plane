#pragma once

#include "GameRuntimeState.h"

#include <string>

class LocalPerformanceLogger final
{
public:
	static bool IsAimingSnapshotPending(
		const AimingPerformanceSnapshot& snapshot);
	static void TryWriteAimingSnapshot(
		const AimingPerformanceSnapshot& snapshot,
		int livingEnemyCount,
		const std::string& stageId);
};
