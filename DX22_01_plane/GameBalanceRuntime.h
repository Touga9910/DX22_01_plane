#pragma once

#include "BalanceAutoPlayer.h"
#include "BalanceValidationController.h"
#include "DynamicBalanceController.h"

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

struct GameBalanceAutomationSnapshot final
{
	bool enabled = false;
	bool stopAfterCurrentRunRequested = false;
	bool stopAfterCurrentRunDefault = false;
	int runCount = 0;
	int maximumRuns = 0;
};

struct GameBalanceReadSnapshot final
{
	GameBalanceAutomationSnapshot automation;
	std::vector<std::uint64_t> pendingBallAdjustments;

	bool progressionScalingEnabled = false;
	int currentProgress = 0;
	int nextEnemyHpDelta = 0;
	int progressionHpStart = 0;
	int progressionHpInterval = 0;
	int progressionHpStep = 0;
	int progressionHpMaximumDelta = 0;
	int nextEnemyAttackDelta = 0;
	int progressionAttackStart = 0;
	int progressionAttackInterval = 0;
	int progressionAttackStep = 0;
	int progressionAttackMaximumDelta = 0;

	std::string baselineDifficultyProfile;
	float baselineEnemyHpMultiplier = 1.0f;
	int baselineEnemyAttackDelta = 0;

	bool validationEnabled = false;
	std::string validationExperimentId;
	std::string validationVariantId;
	std::uint32_t validationVariantIndex = 0;
	std::size_t validationVariantCount = 0;
	std::uint32_t runRandomSeed = 0;
	std::uint32_t stageSelectionSeed = 0;
	std::uint32_t routeSelectionSeed = 0;
	std::uint32_t validationSeedIndex = 0;
	std::size_t validationSeedCount = 0;
	int validationMaximumClearedStages = 0;
	bool validationEnduranceMode = false;
	int clearedStageCount = 0;
};

// Private implementation state for balance validation, automation, and scaling.
// Keep frequently changed concrete types out of Game.h consumers.
class GameBalanceRuntime final
{
public:
	GameBalanceAutomationSnapshot CaptureAutomationSnapshot() const;
	GameBalanceReadSnapshot CaptureReadSnapshot(
		int progress,
		int clearedStageCount) const;

	bool IsAutoPlayEnabled() const;
	void SetAutoPlayEnabled(bool enabled);
	void SetStopAfterCurrentRunRequested(bool requested);
	bool IsValidationEnabled() const;
	void SetValidationEnabled(bool enabled);
	bool HasValidationVariant(const std::string& variantId) const;

private:
	friend class Game;

	BalanceAutoPlayer autoPlayer;
	BalanceValidationController validation;
	DynamicBalanceController scaling;

	std::uint32_t runRandomSeed = 0;
	std::uint32_t stageSelectionSeed = 0;
	std::uint32_t routeSelectionSeed = 0;
	std::uint32_t routeSelectionCounter = 0;

	std::string baselineDifficultyProfile = "normal";
	float baselineEnemyHpMultiplier = 1.0f;
	int baselineEnemyAttackDelta = 0;
};
