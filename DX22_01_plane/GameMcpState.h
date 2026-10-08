#pragma once

#include "GameTypes.h"
#include "RunResultSnapshot.h"
#include "StageData.h"
#include "json/json.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class Game;

// Read-only data exported from Game to the MCP serializer.  This keeps the
// wire-format code independent from Game's physical member layout.
struct GameMcpReadSnapshot
{
	struct Physics
	{
		int substepsLastTick = 0;
		std::uint64_t substepLimitCount = 0;
		std::uint64_t tick = 0;
		int stepsLastFrame = 0;
		double droppedSeconds = 0.0;
	} physics{};

	struct MetaProgression
	{
		int activeAscension = 0;
		int selectedAscension = 0;
		int highestUnlockedAscension = 0;
		int totalRuns = 0;
		int totalClears = 0;
		bool persistentRewardsEligible = false;
		std::vector<bool> achievementUnlocked{};
		std::vector<std::string> unlockedBallIds{};
		std::vector<bool> relicUnlocked{};
	} metaProgression{};

	struct PocketRules
	{
		float playerDamageRatio = 0.0f;
		float playerReturnHalfWidth = 0.0f;
		float playerReturnHalfDepth = 0.0f;
		float enemyReturnX = 0.0f;
		float enemyReturnTopEdgeOffset = 0.0f;
		float normalFinisherRatio = 0.0f;
		float midBossFinisherRatio = 0.0f;
		float bossFinisherRatio = 0.0f;
		StageType stageType = StageType::Normal;
		std::size_t returnQueueSize = 0;
	} pocketRules{};

	struct ProgressionScaling
	{
		bool enabled = false;
		int currentProgress = 0;
		int nextEnemyHpDelta = 0;
		int hpStartProgress = 0;
		int hpInterval = 0;
		int hpStep = 0;
		int maximumHpDelta = 0;
		int nextEnemyAttackDelta = 0;
		int attackStartProgress = 0;
		int attackInterval = 0;
		int attackStep = 0;
		int maximumAttackDelta = 0;
	} progressionScaling{};

	struct BalanceValidation
	{
		bool enabled = false;
		std::string experimentId{};
		std::string variantId{};
		std::uint32_t variantIndex = 0;
		std::size_t variantCount = 0;
		std::uint32_t runSeed = 0;
		std::uint32_t stageSelectionSeed = 0;
		std::uint32_t routeSelectionSeed = 0;
		std::uint32_t seedSuiteIndex = 0;
		std::size_t seedSuiteSize = 0;
		int maximumClearedStages = 0;
		bool enduranceMode = false;
		int clearedStageCount = 0;
	} balanceValidation{};

	struct Debug
	{
		bool active = false;
		bool editorOpen = false;
		bool finished = false;
		nlohmann::json stageEditor = nullptr;
	} debug{};

	bool autoplayEnabled = false;
	bool autoplayStopAfterCurrentRun = false;
	bool autoplayStopAfterCurrentRunDefault = false;
	float restHealRatio = 0.0f;
	std::optional<StageData> queuedStageOverride{};
	std::optional<StageData> currentStageOverride{};
	RunResultSnapshot runStatistics{};
	int selectedOfferIndex = 0;
	int selectedHoldIndex = -1;
	std::vector<std::uint64_t> pendingBallAdjustments{};
	std::string baselineDifficultyProfile{};
	float baselineEnemyHpMultiplier = 1.0f;
	int baselineEnemyAttackDelta = 0;
};

class GameMcpReadView final
{
public:
	static GameMcpReadSnapshot Capture(const Game& game);

	GameMcpReadView() = delete;
};
