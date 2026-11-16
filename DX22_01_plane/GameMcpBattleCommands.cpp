#include "GameMcpCommandHandlers.h"
#include "GameMcpJson.h"
#include "Game.h"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "PlayerBall.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

std::optional<nlohmann::json> GameMcpCommandHandlers::TryBattle(
	Game& game,
	const std::string& action,
	const nlohmann::json& arguments,
	const std::string& scene)
{
	using namespace GameMcpJson;

	auto recordBuildDecision = [&game, &arguments]()
	{
		if (arguments.contains("decision_context") &&
			arguments["decision_context"].is_object())
		{
			game.RecordBalanceEvent(
				"mcp_build_decision",
				arguments["decision_context"]);
		}
	};

    if (action == "evaluate_boss_shots" || action == "fire_boss_shot")
    {
		if (scene != "battle" ||
			game.GetBattleState() != BattleState::AimingDirection ||
			!game.AreAllBallsStopped())
            return CommandResult(false, "Boss planning requires a stopped battle in aiming state.");
        const auto choices = game.EvaluateBossShots();
        if (choices.empty()) return CommandResult(false, "No live Armor boss or playable ball.");
        if (action == "evaluate_boss_shots")
        {
            auto result = CommandResult(true, "Evaluated with shared CCD/TOI prediction; gameplay unchanged.");
            result["evaluation"] = choices;
            return result;
        }
        const bool fired = game.FireBossPlannedShot(arguments.value("candidate_id", std::string()),
            arguments.value("state_key", std::string()));
        return CommandResult(fired, fired ? "Fired the selected boss plan." : "Stale or invalid plan. Evaluate again.");
    }

	if (action == "select_ball")
	{
		if (scene != "battle" ||
			game.GetBattleState() != BattleState::AimingDirection)
		{
			return CommandResult(
				false,
				"A ball can only be selected while aiming in battle.");
		}
		const int offerIndex =
			arguments.value("offer_index", -1);
		if (offerIndex < 0 ||
			offerIndex >= game.GetBallOfferCount())
		{
			return CommandResult(
				false,
				"offer_index is outside the available range.");
		}
		game.ApplyBalanceAutoBallSelection(offerIndex);
		recordBuildDecision();
		return CommandResult(true, "Selected the requested ball.");
	}

	if (action == "fire_shot")
	{
		if (scene != "battle" ||
			game.GetBattleState() != BattleState::AimingDirection)
		{
			return CommandResult(
				false,
				"A shot can only be fired while aiming in battle.");
		}
		const std::vector<PlayerBall*> players =
			GameRuntime::World().GetComponents<PlayerBall>();
		if (players.empty() ||
			players[0] == nullptr ||
			!players[0]->IsIdle() ||
			!game.AreAllBallsStopped())
		{
			return CommandResult(
				false,
				"The player ball is not ready to fire.");
		}

		const float directionX =
			arguments.value("direction_x", 0.0f);
		const float directionZ =
			arguments.value("direction_z", 0.0f);
		const float length =
			std::sqrt(
				directionX * directionX +
				directionZ * directionZ);
		if (!std::isfinite(length) || length <= 0.0001f)
		{
			return CommandResult(
				false,
				"The shot direction must be finite and non-zero.");
		}

		const float power = std::clamp(
			arguments.value("power", 0.0f),
			1.0f,
			8.0f);
		if (!std::isfinite(power))
		{
			return CommandResult(
				false,
				"The shot power must be finite.");
		}

		const DirectX::SimpleMath::Vector3 direction(
			directionX / length,
			0.0f,
			directionZ / length);
		const nlohmann::json telemetry =
			arguments.contains("telemetry") &&
			arguments["telemetry"].is_object()
			? arguments["telemetry"]
			: nlohmann::json::object();
		if (!game.TryFireAutomatedShot(
			direction * power,
			-1,
			&telemetry))
		{
			return CommandResult(
				false,
				"The shot is no longer valid for the current battle state.");
		}
		return CommandResult(true, "Fired the requested shot.");
	}

	return std::nullopt;
}
