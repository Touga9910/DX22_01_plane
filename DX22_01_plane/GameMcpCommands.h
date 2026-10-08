#pragma once

#include "json/json_fwd.hpp"

#include <cstdint>
#include <string>

class Game;
struct StageData;

// MCP command handling may request a small set of privileged mutations, but it
// must not depend on Game's physical member layout.  Keep those requests named
// and narrow here; ordinary gameplay operations continue through Game's public
// validated commands.
class GameMcpCommands final
{
public:
	static nlohmann::json InspectStageLayout(
		const Game& game,
		const nlohmann::json& layout);
	static void ProposeStageLayout(
		Game& game,
		const nlohmann::json& layout,
		std::uint64_t expectedRevision);
	static float GetDefaultPlayerRadius(const Game& game);
	static void QueueNextStageOverride(Game& game, StageData stage);
	static void ClearNextStageOverride(Game& game);
	static void PrunePendingBallAdjustments(Game& game);
	static void DisableAutoPlayForExternalControl(Game& game);
	static bool HasValidationVariant(
		const Game& game,
		const std::string& variantId);
	static bool ApplyExtraMoneyReward(Game& game, int amount);
};
