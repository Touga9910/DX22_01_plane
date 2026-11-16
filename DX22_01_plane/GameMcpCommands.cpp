#include "GameMcpCommands.h"
#include "Game.h"
#include "GameRunDefaults.h"
#include "GameBalanceRuntime.h"
#include "GameStageOverrideState.h"
#include "GameDebugController.h"
#include "RunController.h"
#include "json/json.hpp"

nlohmann::json GameMcpCommands::InspectStageLayout(
	const Game& game,
	const nlohmann::json& layout)
{
	const GameDebugController& debug = *game.m_DebugController;
	return StageLayoutEditor::Inspect(
		layout,
		debug.GetEnemyCatalog(),
		debug.StageEditorPlayerRadius());
}

void GameMcpCommands::ProposeStageLayout(
	Game& game,
	const nlohmann::json& layout,
	std::uint64_t expectedRevision)
{
	GameDebugController& debug = *game.m_DebugController;
	debug.GetStageEditor().Propose(
		layout,
		expectedRevision,
		debug.GetEnemyCatalog(),
		debug.StageEditorPlayerRadius());
}

float GameMcpCommands::GetDefaultPlayerRadius(const Game& game)
{
	return game.m_RunDefaults->PlayerRadius();
}

void GameMcpCommands::QueueNextStageOverride(Game& game, StageData stage)
{
	game.m_StageOverrideState->Queue(std::move(stage));
}

void GameMcpCommands::ClearNextStageOverride(Game& game)
{
	game.m_StageOverrideState->ClearQueued();
}

void GameMcpCommands::PrunePendingBallAdjustments(Game& game)
{
	game.PruneBalanceAutoPendingBalls();
}

void GameMcpCommands::DisableAutoPlayForExternalControl(Game& game)
{
	game.m_BalanceRuntime->SetAutoPlayEnabled(false);
}

bool GameMcpCommands::HasValidationVariant(
	const Game& game,
	const std::string& variantId)
{
	return game.m_BalanceRuntime->HasValidationVariant(variantId);
}

bool GameMcpCommands::ApplyExtraMoneyReward(Game& game, int amount)
{
	if (!game.IsClearRewardActive() ||
		game.IsClearRewardChosen() ||
		game.IsMidBossRelicSelectionActive() ||
		amount <= 0)
	{
		return false;
	}
	game.m_RunController->AddMoney(amount);
	return true;
}
