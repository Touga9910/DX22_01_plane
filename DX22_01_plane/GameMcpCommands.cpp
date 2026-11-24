#include "GameMcpCommands.h"
#include "Game.h"
#include "GameRunDefaults.h"
#include "GameBalanceRuntime.h"
#include "GameStageOverrideState.h"
#include "GameDebugController.h"
#include "RunController.h"
#include "json/json.hpp"

/// <summary>
/// 敵カタログと自球半径を使って、指定されたステージ配置を検査する。
/// </summary>
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

/// <summary>
/// 指定されたリビジョンを基準にステージ配置の変更案を登録する。
/// </summary>
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

/// <summary>
/// ランの既定値に設定された自球の半径を返す。
/// </summary>
float GameMcpCommands::GetDefaultPlayerRadius(const Game& game)
{
	return game.m_RunDefaults->PlayerRadius();
}

/// <summary>
/// 次のステージで使用する配置データを予約する。
/// </summary>
void GameMcpCommands::QueueNextStageOverride(Game& game, StageData stage)
{
	game.m_StageOverrideState->Queue(std::move(stage));
}

/// <summary>
/// 次のステージに予約されている配置データを解除する。
/// </summary>
void GameMcpCommands::ClearNextStageOverride(Game& game)
{
	game.m_StageOverrideState->ClearQueued();
}

/// <summary>
/// 現在の状態に合わなくなった自動プレイの球調整候補を除外する。
/// </summary>
void GameMcpCommands::PrunePendingBallAdjustments(Game& game)
{
	game.PruneBalanceAutoPendingBalls();
}

/// <summary>
/// 外部操作に切り替えるため、ゲーム内の自動プレイを無効化する。
/// </summary>
void GameMcpCommands::DisableAutoPlayForExternalControl(Game& game)
{
	game.m_BalanceRuntime->SetAutoPlayEnabled(false);
}

/// <summary>
/// 指定IDのバランス検証バリアントが登録されているかを返す。
/// </summary>
bool GameMcpCommands::HasValidationVariant(
	const Game& game,
	const std::string& variantId)
{
	return game.m_BalanceRuntime->HasValidationVariant(variantId);
}

/// <summary>
/// クリア報酬が選択可能な状態であれば、正の追加金額を所持金に加える。
/// </summary>
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
