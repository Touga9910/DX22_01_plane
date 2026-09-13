#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "json/json.hpp"

class Game;

// 制限されたゲーム状態のスナップショットと、保留中のコマンド1件を
// ローカルMCPサーバーと交換する。状態変更コマンドの検証と適用に関する
// 最終的な決定権は、ゲーム側が保持する。
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
	int m_PublishIntervalFrames = 60;
	int m_FramesUntilPublish = 0;
	int m_CommandPollIntervalFrames = 6;
	int m_FramesUntilCommandPoll = 0;
	std::uint64_t m_StateSequence = 0;
	std::string m_LastCommandId;
	std::vector<std::string> m_StageEnemyIds;
	std::filesystem::path m_BridgeDirectory =
		"runtime/game_mcp";
};
