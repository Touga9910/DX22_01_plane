#pragma once

#include "json/json_fwd.hpp"
#include <string>
#include <vector>

class Game;
class PlayerBall;
class EnemyBall;
struct GameMcpReadSnapshot;

// Internal publication sections. Call order belongs to BuildState; the snapshot
// and enemy ordering are shared for the duration of one publication only.
namespace GameMcpStateSerialization
{
	void AppendBattleDiagnostics(Game& game, const GameMcpReadSnapshot& snapshot, nlohmann::json& state);
	void AppendRunMode(Game& game, const GameMcpReadSnapshot& snapshot, nlohmann::json& state);
	void AppendPerformanceDiagnostics(Game& game, nlohmann::json& state);
	void AppendProgression(Game& game, const GameMcpReadSnapshot& snapshot, nlohmann::json& state);
	void AppendTable(Game& game, const GameMcpReadSnapshot& snapshot, const std::vector<std::string>& stageEnemyIds, nlohmann::json& state);
	const PlayerBall* AppendPlayer(Game& game, const GameMcpReadSnapshot& snapshot, nlohmann::json& state);
	void AppendRunProgress(Game& game, const GameMcpReadSnapshot& snapshot, nlohmann::json& state);
	void AppendSelectedBall(Game& game, const GameMcpReadSnapshot& snapshot, const PlayerBall* player, nlohmann::json& state);
	void AppendRelics(Game& game, const GameMcpReadSnapshot& snapshot, nlohmann::json& state);
	std::vector<EnemyBall*> AppendEnemies(Game& game, nlohmann::json& state);
	void AppendInventory(Game& game, const GameMcpReadSnapshot& snapshot, nlohmann::json& state);
	void AppendBalance(Game& game, const GameMcpReadSnapshot& snapshot, nlohmann::json& state);
	void AppendAvailableActions(Game& game, const GameMcpReadSnapshot& snapshot, const PlayerBall* player, nlohmann::json& state);
	void AppendSynergies(Game& game, const std::vector<EnemyBall*>& enemies, nlohmann::json& state);
	void RestrictDebugActions(Game& game, const GameMcpReadSnapshot& snapshot, nlohmann::json& state);
}
