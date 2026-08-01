#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "json/json.hpp"

class Game;

// Exchanges a bounded game-state snapshot and one pending command with a
// local MCP server. The game remains the authority that validates and applies
// every state-changing command.
class GameMcpBridge
{
public:
	bool Initialize(
		Game& game,
		const std::string& configPath =
			"assets/data/game_mcp_bridge.json");
	void Update(Game& game);
	void Shutdown(Game& game);

	bool IsEnabled() const { return m_Enabled; }

private:
	nlohmann::json BuildState(
		Game& game,
		bool running) const;
	nlohmann::json ExecuteCommand(
		Game& game,
		const nlohmann::json& command);
	void ProcessPendingCommand(Game& game);
	void PublishState(Game& game, bool running);

	std::filesystem::path GetStatePath() const;
	std::filesystem::path GetCommandPath() const;
	std::filesystem::path GetResultPath() const;

private:
	bool m_Enabled = false;
	bool m_AllowWriteActions = true;
	int m_PublishIntervalFrames = 10;
	int m_FramesUntilPublish = 0;
	std::uint64_t m_StateSequence = 0;
	std::string m_LastCommandId;
	std::vector<std::string> m_StageEnemyIds;
	std::filesystem::path m_BridgeDirectory =
		"runtime/game_mcp";
};
