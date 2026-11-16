#pragma once

#include "json/json_fwd.hpp"
#include <SimpleMath.h>
#include <cstddef>
#include <cstdint>
#include <string>

class Game;
class Scene;
struct PlayerBallData;
enum class RunCombatMode;
enum class StageType;

// Wire-format helpers and historical protocol constants shared by MCP domains.
namespace GameMcpJson
{
	inline constexpr int kShopRemoveCost = 15;
	inline constexpr int kShopBallCost = 20;
	inline constexpr int kExtraRewardMoney = 10;
	inline constexpr std::size_t kMaximumMcpStageEnemyCount = 16;

	std::int64_t UnixTimeMilliseconds();
	const char* GetGameStateName(const Game& game);
	const char* GetSceneName(const Scene* scene);
	nlohmann::json VectorToJson(const DirectX::SimpleMath::Vector3& vector);
	const char* StageTypeToMcpString(StageType stageType);
	nlohmann::json BallDataToJson(const PlayerBallData& ball, int index);
	int FindDeckBallIndex(const Game& game, std::uint64_t instanceId);
	const char* McpRunCombatModeId(RunCombatMode mode);
	nlohmann::json CommandResult(bool ok, const std::string& message);
}
