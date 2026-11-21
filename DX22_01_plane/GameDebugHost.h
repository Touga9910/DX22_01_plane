#pragma once

#include <cstdint>
#include <vector>

struct DebugBattleSetup;
struct DebugRunSnapshot;
class Game;
struct PlayerBallData;
struct StageData;
struct DebugRunSetup;

struct DebugAutomationSnapshot
{
	bool enabled = false;
	bool stopAfterCurrentRunRequested = false;
	bool stopAfterCurrentRunDefault = false;
	int runCount = 0;
	int maximumRuns = 0;
};

// Narrow privileged boundary used by the debug controller.  The controller
// owns the debug workflow and UI; this host alone knows Game's member layout.
class GameDebugHost final
{
public:
	static bool CanOpenEditor(const Game& game);
	static void StartRun(Game& game, const DebugRunSetup& setup, std::uint32_t seed);
	static bool JumpRun(Game& game, const DebugRunSetup& setup);
	static bool CanJumpRun(const Game& game);
	static bool WinBattle(Game& game);
	static void SetInfiniteMoney(Game& game, bool enabled);
	static void CaptureSetup(Game& game, DebugBattleSetup& setup);
	static bool CaptureRunEntry(const Game& game, DebugRunSetup& setup);
	static void RestartRunBattle(Game& game, StageData stage);
	static DebugRunSnapshot CaptureRunSnapshot(const Game& game);
	static void PrepareSession(Game& game, const DebugBattleSetup& setup);
	static void ApplyRunSettings(
		Game& game,
		const DebugBattleSetup& setup,
		const std::vector<PlayerBallData>& ballCatalog);
	static void StartBattle(
		Game& game,
		std::uint32_t seed,
		StageData stage);
	static void EndSession(Game& game, const DebugRunSnapshot& snapshot);
	static void CaptureCurrentPlayerStatus(Game& game);
	static void SetPaused(Game& game, bool paused);
	static void UpdateExternalBridge(Game& game);
	static float GetDefaultRestHealRatio(const Game& game);
	static DebugAutomationSnapshot CaptureAutomation(const Game& game);
	static void SetAutoPlayEnabled(Game& game, bool enabled);
	static void SetStopAfterCurrentRunRequested(Game& game, bool requested);
	static void SaveSnapshot(Game& game);
};
