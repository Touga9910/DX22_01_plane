#include "GameMcpCommandHandlers.h"
#include "GameMcpCommands.h"
#include "GameMcpJson.h"
#include "Game.h"
#include "GameTypes.h"
#include "RestSiteScene.h"
#include "RunEventView.h"
#include "StageSelectScene.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>

/// <summary>
/// ランの新規開始、進路選択、次の戦闘や報酬後への進行コマンドを検証して処理する。
/// </summary>
std::optional<nlohmann::json> GameMcpCommandHandlers::TryRun(
	Game& game,
	const std::string& action,
	const nlohmann::json& arguments,
	const std::string& scene)
{
	using namespace GameMcpJson;

	if (action == "start_new_run")
	{
		if (scene != "title" && scene != "result")
		{
			return CommandResult(
				false,
				"A new run can only start from the title or result scene.");
		}
		GameMcpCommands::DisableAutoPlayForExternalControl(game);
		std::optional<std::uint32_t> forcedRandomSeed;
		if (arguments.contains("run_seed"))
		{
			if (!arguments["run_seed"].is_number_unsigned() &&
				!arguments["run_seed"].is_number_integer())
			{
				return CommandResult(false, "run_seed must be an integer.");
			}
			const long long seed = arguments["run_seed"].get<long long>();
			if (seed < 0 || seed > 0xffffffffll)
			{
				return CommandResult(
					false,
					"run_seed must be between 0 and 4294967295.");
			}
			forcedRandomSeed = static_cast<std::uint32_t>(seed);
		}
		const std::string forcedValidationVariant = arguments.value(
			"validation_variant",
			std::string());
		if (!forcedValidationVariant.empty())
		{
			const bool found = GameMcpCommands::HasValidationVariant(
				game,
				forcedValidationVariant);
			if (!found)
			{
				return CommandResult(
					false,
					"validation_variant does not exist.");
			}
		}
		std::optional<RunCombatMode> requestedRunMode;
		std::string requestedModeId = "legacy";
		if (arguments.contains("combat_mode"))
		{
			if (!arguments["combat_mode"].is_string())
			{
				return CommandResult(false, "combat_mode must be a string.");
			}
			requestedModeId = arguments["combat_mode"].get<std::string>();
			if (requestedModeId == "phase")
			{
				requestedRunMode = RunCombatMode::PhaseTest;
			}
			else if (requestedModeId == "legacy")
			{
				requestedRunMode = RunCombatMode::Legacy;
			}
			else
			{
				return CommandResult(
					false, "combat_mode must be legacy or phase.");
			}
		}
		if (arguments.contains("run_mode"))
		{
			if (!arguments["run_mode"].is_string())
			{
				return CommandResult(false, "run_mode must be a string.");
			}
			const std::string legacyModeId =
				arguments["run_mode"].get<std::string>();
			if (legacyModeId != "legacy" && legacyModeId != "phase_test")
			{
				return CommandResult(
					false, "run_mode must be legacy or phase_test.");
			}
			const RunCombatMode legacyRequestedMode =
				RunCombatModeFromString(legacyModeId);
			if (requestedRunMode.has_value() &&
				requestedRunMode.value() != legacyRequestedMode)
			{
				return CommandResult(
					false, "combat_mode conflicts with legacy run_mode.");
			}
			requestedRunMode = legacyRequestedMode;
			requestedModeId = McpRunCombatModeId(legacyRequestedMode);
		}
		const RunCombatMode runCombatMode = requestedRunMode.value_or(
			RunCombatMode::Legacy);
		const std::string phaseProgressionId = arguments.value(
			"phase_progression_mode",
			std::string("encounter"));
		if (phaseProgressionId != "encounter" &&
			phaseProgressionId != "route")
		{
			return CommandResult(
				false,
				"phase_progression_mode must be encounter or route.");
		}
		const PhaseRunProgressionMode phaseProgressionMode =
			PhaseRunProgressionModeFromString(phaseProgressionId);
		const std::string mcpShotPolicy = arguments.value(
			"mcp_shot_policy",
			std::string("standard"));
		if (mcpShotPolicy != "standard" && mcpShotPolicy != "tactical")
		{
			return CommandResult(
				false,
				"mcp_shot_policy must be standard or tactical.");
		}
		game.StartNewRun(
			"mcp",
			arguments.value(
				"controller_profile",
				std::string("unknown")),
			arguments.value(
				"build_profile",
				std::string("unknown")),
			arguments.value(
				"build_profile_settings_hash",
				std::string()),
			forcedRandomSeed,
			forcedValidationVariant,
			runCombatMode,
			phaseProgressionMode,
			mcpShotPolicy);
		const RunCombatMode appliedRunMode = game.GetRunCombatMode();
		if (appliedRunMode != runCombatMode)
		{
			return CommandResult(
				false,
				"Requested combat mode was not applied; run start aborted.");
		}
		game.ChangeScene(SceneType::Select);
		nlohmann::json result = CommandResult(
			true,
			"Started a new " + requestedModeId + " run.");
		result["requested_combat_mode"] = requestedModeId;
		result["applied_combat_mode"] =
			McpRunCombatModeId(appliedRunMode);
		result["phase_progression_mode"] = phaseProgressionId;
		result["mcp_shot_policy"] = mcpShotPolicy;
		result["run_seed"] = forcedRandomSeed.has_value()
			? nlohmann::json(forcedRandomSeed.value())
			: nlohmann::json(nullptr);
		return result;
	}

	if (action == "choose_destination")
	{
		if (scene != "stage_select")
		{
			return CommandResult(
				false,
				"A destination can only be chosen from stage select.");
		}
		StageSelectScene* stageSelect =
			dynamic_cast<StageSelectScene*>(game.GetCurrentScene());
		if (stageSelect == nullptr)
		{
			return CommandResult(
				false,
				"Route options are unavailable in the current scene.");
		}
		const int routeIndex = arguments.value("route_index", -1);
		if (routeIndex < 0 ||
			routeIndex >= stageSelect->GetRouteNodeCount())
		{
			return CommandResult(
				false,
				"route_index must identify one of the current route_options.");
		}
		const std::string selectedDestination =
			stageSelect->GetRouteIdAt(routeIndex);
		if (!stageSelect->ChooseRoute(routeIndex, "mcp"))
		{
			return CommandResult(false, "Failed to enter the selected route.");
		}
		return CommandResult(
			true,
			"Entered route option " + std::to_string(routeIndex) +
			" (" + selectedDestination + ").");
	}

	if (action == "choose_run_event")
	{
		if (scene != "event")
		{
			return CommandResult(false, "There is no active run event.");
		}
		const auto choice = arguments.value("choice_index", nlohmann::json(-1));
		const auto target = arguments.value("target_index", nlohmann::json(-1));
		const auto maxIndex = (std::numeric_limits<int>::max)();
		const auto isValidIndex = [maxIndex](const nlohmann::json& index, int minIndex)
		{
			if (index.is_number_unsigned())
			{
				return index.get<std::uint64_t>() <= static_cast<std::uint64_t>(maxIndex);
			}
			return index.is_number_integer() && index >= minIndex && index <= maxIndex;
		};
		if (!isValidIndex(choice, 0) || !isValidIndex(target, -1))
		{
			return CommandResult(false, "The choice or target index is invalid.");
		}
		if (!game.ChooseRunEvent(choice.get<int>(), target.get<int>()))
		{
			return CommandResult(false, "The event choice or target is unavailable.");
		}
		return CommandResult(true, "Applied the event choice.");
	}

	if (action == "continue_run_event")
	{
		if (arguments.contains("repeat") && !arguments.at("repeat").is_boolean())
			return CommandResult(false, "repeat must be a boolean.");
		return CommandResult(scene == "event" && game.ContinueRunEvent(arguments.value("repeat", false)),
			"Continue the acknowledged event result.");
	}

	if (action == "continue_to_battle")
	{
		if (scene == "event")
		{
			const auto event = game.CaptureRunEvent();
			if (event.result.pending)
				return CommandResult(game.ContinueRunEvent(), "Continue the event result.");
			if (event.choices.empty() || !game.ChooseRunEvent(
				static_cast<int>(event.choices.size()) - 1, -1))
			{
				return CommandResult(false, "There is no active run event to leave.");
			}
			return CommandResult(true, "Prepared the leave result. Confirm it with continue_run_event.");
		}
		if (scene != "rest_site" && scene != "shop")
		{
			return CommandResult(
				false,
				"This action is only available at a rest site, shop or event.");
		}
		if (scene == "rest_site")
		{
			RestSiteScene* restSite =
				dynamic_cast<RestSiteScene*>(game.GetCurrentScene());
			if (restSite != nullptr &&
				!restSite->HasUsedAction() &&
				game.HasAvailableRestBenefit())
			{
				return CommandResult(
					false,
					"Choose heal or upgrade_ball before leaving the rest site.");
			}
		}
		if (scene == "rest_site")
		{
			game.LeaveRestSite();
		}
		else
		{
			game.LeaveShop();
		}
		return CommandResult(true, "Completed the area and returned to the route.");
	}

	if (action == "continue_after_reward")
	{
		if (!game.IsClearRewardActive() ||
			!game.IsClearRewardChosen())
		{
			return CommandResult(
				false,
				"Choose a clear reward before continuing.");
		}
		game.ContinueAfterClearReward();
		return CommandResult(true, "Returned to stage select.");
	}

	return std::nullopt;
}
