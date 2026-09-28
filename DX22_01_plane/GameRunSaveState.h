#pragma once

#include "GameTypes.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "RunProgressController.h"
#include "RunResultSnapshot.h"
#include "StageSelector.h"

#include <array>
#include <cstdint>
#include <random>
#include <vector>

// Typed boundary between the run-save document and Game's internal owners.
struct GameRunSaveSnapshot
{
	PlayerRunStatus status{};
	RunProgressState progress{};
	std::array<bool, static_cast<std::size_t>(RelicType::Count)> relics{};
	PlayerDeckSnapshot deck{};
	StageSelectorSnapshot stageSelector{};
	std::vector<int> shopRelicOffers{};
	RunResultSnapshot statistics{};
	int activeAscension = 0;
	std::uint32_t runSeed = 0;
	std::uint32_t stageSelectionSeed = 0;
	std::uint32_t routeSelectionSeed = 0;
	std::uint32_t routeSelectionCounter = 0;
	std::mt19937 pocketRandomEngine{};
	std::mt19937 relicRandomEngine{};
};

// Restore intent is applied through Game's consistency boundary.
struct GameRunRestoreRequest
{
	GameRunSaveSnapshot snapshot{};
	SceneType resumeScene = SceneType::Select;
	bool restActionUsed = false;
	bool migratedToBossPreparation = false;
	bool hasRelicRandomState = false;
};
