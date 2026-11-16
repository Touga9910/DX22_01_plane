#include "GameMcpStateSerialization.h"
#include "GameMcpJson.h"
#include "Game.h"
#include "GameMcpState.h"
#include "json/json.hpp"
#include "GameMcpBridge.h"
#include "GameMcpCommands.h"
#include "TableConfig.h"

using namespace GameMcpJson;

nlohmann::json GameMcpBridge::BuildState(Game& game, bool running) const
{
	GameMcpCommands::PrunePendingBallAdjustments(game);
	const GameMcpReadSnapshot snapshot = GameMcpReadView::Capture(game);
	nlohmann::json state = {
		{ "schema_version", 1 },
		{ "running", running },
		{ "published_at_unix_ms", UnixTimeMilliseconds() },
		{ "sequence", m_StateSequence },
		{ "scene", GetSceneName(game.GetCurrentScene()) },
		{ "game_state", GetGameStateName(game) },
		{ "battle_state", ToString(game.GetBattleState()) },
		{ "battle_result", ToString(game.GetLastBattleResult()) },
		{ "clear_reward_active", game.IsClearRewardActive() },
		{ "selected_stage_id", game.GetSelectedStageId() },
		{ "autoplay_enabled", snapshot.autoplayEnabled },
		{ "autoplay_stop_after_current_run",
			snapshot.autoplayStopAfterCurrentRun },
		{ "autoplay_stop_after_current_run_default",
			snapshot.autoplayStopAfterCurrentRunDefault },
		{ "bridge", {
			{ "write_actions_enabled", m_AllowWriteActions },
			{ "single_pending_command", true },
		} },
	};
	// Preserve publication order, including the single prune/capture above.
	using namespace GameMcpStateSerialization;
	AppendBattleDiagnostics(game, snapshot, state);
	AppendRunMode(game, snapshot, state);
	AppendPerformanceDiagnostics(game, state);
	state["table"] = {
		{ "field_width", TableConfig::GetFieldWidth() },
		{ "field_depth", TableConfig::GetFieldDepth() },
		{ "walls", nlohmann::json::array() },
		{ "pockets", nlohmann::json::array() },
	};
	AppendProgression(game, snapshot, state);
	AppendTable(game, snapshot, m_StageEnemyIds, state);
	const PlayerBall* player = AppendPlayer(game, snapshot, state);
	AppendRunProgress(game, snapshot, state);
	AppendSelectedBall(game, snapshot, player, state);
	AppendRelics(game, snapshot, state);
	const std::vector<EnemyBall*> enemies = AppendEnemies(game, state);
	AppendInventory(game, snapshot, state);
	AppendBalance(game, snapshot, state);
	AppendAvailableActions(game, snapshot, player, state);
	state["debug_mode"] = {
		{"active", snapshot.debug.active},
		{"editor_open", snapshot.debug.editorOpen},
		{"finished", snapshot.debug.finished},
	};
	if (!snapshot.debug.stageEditor.is_null())
	{
		state["stage_editor"] = snapshot.debug.stageEditor;
	}
	AppendSynergies(game, enemies, state);
	RestrictDebugActions(game, snapshot, state);
	return state;
}
