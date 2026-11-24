#include "GameMcpCommandHandlers.h"
#include "GameMcpCommands.h"
#include "GameMcpJson.h"
#include "Collision.h"
#include "Game.h"
#include "StageData.h"
#include "StageDataLoader.h"
#include "TableConfig.h"
#include "TitleScene.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
	/// <summary>
	/// ステージ配置IDの文字数と使用文字が許容範囲に収まるかを判定する。
	/// </summary>
	bool IsValidLayoutId(const std::string& layoutId)
	{
		if (layoutId.empty() || layoutId.size() > 64)
		{
			return false;
		}
		return std::all_of(
			layoutId.begin(),
			layoutId.end(),
			[](unsigned char character)
			{
				return std::isalnum(character) != 0 ||
					character == '_' ||
					character == '-' ||
					character == '.';
			});
	}

	/// <summary>
	/// MCP用の識別子をステージ種別に変換し、通常戦を既定値として返す。
	/// </summary>
	StageType ParseStageType(const std::string& value)
	{
		if (value == "midboss")
		{
			return StageType::MidBoss;
		}
		if (value == "boss")
		{
			return StageType::Boss;
		}
		return StageType::Normal;
	}
}

/// <summary>
/// タイトルからのステージエディター起動、配置の検証と変更案の提示を処理する。
/// </summary>
std::optional<nlohmann::json> GameMcpCommandHandlers::TryStageEditor(
	Game& game,
	const std::string& action,
	const nlohmann::json& command)
{
	using namespace GameMcpJson;

	if (action == "open_stage_editor")
	{
		if (dynamic_cast<TitleScene*>(game.GetCurrentScene()) == nullptr) return CommandResult(false, "Open the stage editor from the title screen.");
		game.OpenDebugMode();
		return CommandResult(true, "Stage editor opened in debug setup.");
	}
	if (action == "validate_stage_layout" || action == "propose_stage_layout")
	{
		if (!game.IsDebugEditorOpen()) return CommandResult(false, "Open the stage editor first.");
		const auto args = command.value("arguments", nlohmann::json::object());
		const auto layout = args.value("layout", nlohmann::json());
		const auto report = GameMcpCommands::InspectStageLayout(game, layout);
		auto result = CommandResult(report["valid"].get<bool>(), "Layout validation complete.");
		result["report"] = report;
		if (action == "propose_stage_layout" && report["valid"].get<bool>())
		{
			try
			{
				const auto& expected = args.at("expected_revision");
				if (!expected.is_number_integer() || expected.get<double>() < 0) throw std::runtime_error("expected_revision must be a nonnegative integer.");
				GameMcpCommands::ProposeStageLayout(
					game,
					layout,
					expected.get<std::uint64_t>());
				result["message"] = "Proposal ready for visual review. Draft and files are unchanged until accepted in the editor.";
			}
			catch (const std::exception& e) { result = CommandResult(false, e.what()); result["report"] = report; }
		}
		return result;
	}

	return std::nullopt;
}

/// <summary>
/// 次のステージに使用する配置の指定や解除を検証して処理する。
/// </summary>
std::optional<nlohmann::json> GameMcpCommandHandlers::TryStageLayout(
	Game& game,
	const std::string& action,
	const nlohmann::json& arguments)
{
	using namespace GameMcpJson;

	if (action == "set_next_stage_layout")
	{
		if (!arguments.contains("layout_id") ||
			!arguments["layout_id"].is_string())
		{
			return CommandResult(
				false,
				"layout_id must be a string.");
		}
		const std::string layoutId =
			arguments["layout_id"].get<std::string>();
		if (!IsValidLayoutId(layoutId))
		{
			return CommandResult(
				false,
				"layout_id must be 1-64 characters using letters, numbers, _, -, or ..");
		}

		const std::string stageTypeName =
			arguments.value(
				"stage_type",
				std::string("normal"));
		if (stageTypeName != "normal" &&
			stageTypeName != "midboss" &&
			stageTypeName != "boss")
		{
			return CommandResult(
				false,
				"stage_type must be normal, midboss, or boss.");
		}

		if (!arguments.contains("difficulty") ||
			!arguments["difficulty"].is_number_integer() ||
			!arguments.contains("par") ||
			!arguments["par"].is_number_integer())
		{
			return CommandResult(
				false,
				"difficulty and par must be integers.");
		}
		const int difficulty =
			arguments["difficulty"].get<int>();
		const int par = arguments["par"].get<int>();
		if (difficulty < 1 || difficulty > 99 ||
			par < 1 || par > 99)
		{
			return CommandResult(
				false,
				"difficulty and par must be between 1 and 99.");
		}

		if (!arguments.contains("enemies") ||
			!arguments["enemies"].is_array())
		{
			return CommandResult(
				false,
				"enemies must be an array.");
		}
		const nlohmann::json& enemyArray =
			arguments["enemies"];
		if (enemyArray.empty() ||
			enemyArray.size() > kMaximumMcpStageEnemyCount)
		{
			return CommandResult(
				false,
				"enemies must contain between 1 and 12 placements.");
		}

		std::unordered_map<std::string, EnemyData>
			enemyDefinitions;
		for (const EnemyData& enemy :
			StageDataLoader::LoadEnemyDefinitions(
				"assets/data/enemy_data.json"))
		{
			enemyDefinitions.emplace(enemy.id, enemy);
		}
		if (enemyDefinitions.empty())
		{
			return CommandResult(
				false,
				"Enemy definitions could not be loaded.");
		}

		StageData stage;
		stage.id = layoutId;
		stage.stageType = ParseStageType(stageTypeName);
		stage.difficulty = difficulty;
		stage.par = par;
		stage.enemies.reserve(enemyArray.size());

		const float fieldHalfWidth =
			TableConfig::GetFieldWidth() * 0.5f;
		const float fieldHalfDepth =
			TableConfig::GetFieldDepth() * 0.5f;
		const float playerRadius = (std::max)(
			0.01f,
			GameMcpCommands::GetDefaultPlayerRadius(game));
		const Collision::Sphere playerSpawn = {
			DirectX::SimpleMath::Vector3(
				0.0f,
				TableConfig::FIELD_HEIGHT,
				0.0f),
			playerRadius,
		};
		std::vector<Collision::Sphere> placedEnemies;
		placedEnemies.reserve(enemyArray.size());

		for (std::size_t enemyIndex = 0;
			enemyIndex < enemyArray.size();
			enemyIndex++)
		{
			const nlohmann::json& placement =
				enemyArray[enemyIndex];
			if (!placement.is_object() ||
				!placement.contains("enemy_id") ||
				!placement["enemy_id"].is_string() ||
				!placement.contains("x") ||
				!placement["x"].is_number() ||
				!placement.contains("z") ||
				!placement["z"].is_number())
			{
				return CommandResult(
					false,
					"Each enemy placement requires enemy_id, x, and z.");
			}

			const std::string enemyId =
				placement["enemy_id"].get<std::string>();
			const auto definitionIt =
				enemyDefinitions.find(enemyId);
			if (definitionIt == enemyDefinitions.end())
			{
				return CommandResult(
					false,
					"Unknown enemy_id at placement " +
						std::to_string(enemyIndex) +
						": " + enemyId + ".");
			}

			const double requestedX =
				placement["x"].get<double>();
			const double requestedZ =
				placement["z"].get<double>();
			if (!std::isfinite(requestedX) ||
				!std::isfinite(requestedZ))
			{
				return CommandResult(
					false,
					"Enemy coordinates must be finite.");
			}

			EnemySpawnData spawn;
			spawn.enemyId = enemyId;
			spawn.enemyData = definitionIt->second;
			spawn.position =
				DirectX::SimpleMath::Vector3(
					static_cast<float>(requestedX),
					TableConfig::FIELD_HEIGHT,
					static_cast<float>(requestedZ));
			spawn.enemyData.initPosition = spawn.position;

			const float radius = (std::max)(
				0.01f,
				spawn.enemyData.status.radius);
			const float edgeClearance =
				TableConfig::POCKET_RADIUS;
			if (std::abs(spawn.position.x) +
					radius + edgeClearance >
					fieldHalfWidth ||
				std::abs(spawn.position.z) +
					radius + edgeClearance >
					fieldHalfDepth)
			{
				return CommandResult(
					false,
					"Enemy placement " +
						std::to_string(enemyIndex) +
						" is outside the safe playable area.");
			}

			const Collision::Sphere enemySphere = {
				spawn.position,
				radius,
			};
			if (Collision::CheckHit(
				enemySphere,
				playerSpawn))
			{
				return CommandResult(
					false,
					"Enemy placement " +
						std::to_string(enemyIndex) +
						" overlaps the player spawn.");
			}
			for (const Collision::Sphere& placed :
				placedEnemies)
			{
				if (Collision::CheckHit(enemySphere, placed))
				{
					return CommandResult(
						false,
						"Enemy placement " +
							std::to_string(enemyIndex) +
							" overlaps another enemy.");
				}
			}

			placedEnemies.push_back(enemySphere);
			stage.enemies.push_back(std::move(spawn));
		}

		GameMcpCommands::QueueNextStageOverride(game, std::move(stage));
		return CommandResult(
			true,
			"Queued MCP stage layout '" + layoutId +
				"' with " +
				std::to_string(enemyArray.size()) +
				" enemies for the next battle.");
	}

	if (action == "clear_next_stage_layout")
	{
		GameMcpCommands::ClearNextStageOverride(game);
		return CommandResult(
			true,
			"Cleared the queued MCP stage layout.");
	}

	return std::nullopt;
}
