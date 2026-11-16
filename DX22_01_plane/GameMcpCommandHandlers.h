#pragma once

#include "json/json.hpp"

#include <optional>
#include <string>

class Game;

namespace GameMcpCommandHandlers
{
	std::optional<nlohmann::json> TryStageEditor(
		Game& game,
		const std::string& action,
		const nlohmann::json& command);
	std::optional<nlohmann::json> TryStageLayout(
		Game& game,
		const std::string& action,
		const nlohmann::json& arguments);
	std::optional<nlohmann::json> TryRun(
		Game& game,
		const std::string& action,
		const nlohmann::json& arguments,
		const std::string& scene);
	std::optional<nlohmann::json> TryBattle(
		Game& game,
		const std::string& action,
		const nlohmann::json& arguments,
		const std::string& scene);
	std::optional<nlohmann::json> TryEconomy(
		Game& game,
		const std::string& action,
		const nlohmann::json& arguments,
		const std::string& scene);
}
