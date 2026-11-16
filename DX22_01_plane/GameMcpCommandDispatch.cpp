#include "GameMcpBridge.h"
#include "GameMcpCommandHandlers.h"
#include "GameMcpJson.h"
#include "Game.h"

#include <utility>

nlohmann::json GameMcpBridge::ExecuteCommand(
	Game& game,
	const nlohmann::json& command)
{
	using namespace GameMcpJson;

	if (!m_AllowWriteActions)
	{
		return CommandResult(
			false,
			"Write actions are disabled by game_mcp_bridge.json.");
	}

	const std::string action =
		command.value("action", std::string());
	if (auto result = GameMcpCommandHandlers::TryStageEditor(
		game, action, command))
	{
		return std::move(*result);
	}
	if (game.IsDebugEditorOpen())
		return CommandResult(false, "Debug setup is open. Resume or start the battle from its window.");
	if (game.IsDebugMode() && action != "select_ball" && action != "fire_shot" &&
		action != "evaluate_boss_shots" && action != "fire_boss_shot")
		return CommandResult(false, "This action is unavailable in debug battle mode.");
	const nlohmann::json arguments =
		command.value(
			"arguments",
			nlohmann::json::object());
	const std::string scene = GetSceneName(game.GetCurrentScene());

	if (auto result = GameMcpCommandHandlers::TryBattle(
		game, action, arguments, scene))
	{
		return std::move(*result);
	}
	if (auto result = GameMcpCommandHandlers::TryStageLayout(
		game, action, arguments))
	{
		return std::move(*result);
	}
	if (auto result = GameMcpCommandHandlers::TryRun(
		game, action, arguments, scene))
	{
		return std::move(*result);
	}
	if (auto result = GameMcpCommandHandlers::TryEconomy(
		game, action, arguments, scene))
	{
		return std::move(*result);
	}

	return CommandResult(false, "Unknown game-control action.");
}
