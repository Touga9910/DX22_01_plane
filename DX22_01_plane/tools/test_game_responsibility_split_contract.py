from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def source(name: str) -> str:
    return (ROOT / name).read_text(encoding="utf-8-sig")


class GameResponsibilitySplitContractTests(unittest.TestCase):
    def test_game_header_keeps_heavy_implementation_headers_private(self) -> None:
        game = source("Game.h")

        self.assertIn('#include "json/json_fwd.hpp"', game)
        for implementation_header in (
            '#include "json/json.hpp"',
            '#include "BossShotPlanner.h"',
            '#include "GameDebugController.h"',
            '#include "RunProgressController.h"',
        ):
            self.assertNotIn(implementation_header, game)

    def test_battle_state_and_turn_flow_are_owned_by_battle_controller(self) -> None:
        game = source("Game.h")
        battle = source("BattleController.h") + source("BattleController.cpp")

        self.assertNotIn("enum class GameState", game)
        self.assertNotIn("m_GameState", game)
        self.assertIn("BattleState m_State", battle)
        self.assertIn("void BattleController::ProcessEnemyAttack()", battle)
        self.assertIn("void BattleController::ProcessTurnEnd()", battle)
        self.assertIn("BattleResult BattleController::ConsumeResult()", battle)
        for battle_member in (
            "m_ShotRelicRules",
            "m_PocketedEnemyQueue",
            "m_StageType",
            "m_BountyRewardClaimed",
        ):
            self.assertIn(battle_member, battle)
            self.assertNotIn(battle_member, game)
        for old_shot_member in (
            "m_CurrentShotCollisionAttackBonus",
            "m_CurrentShotPlayerEnemyCollisionCount",
            "m_CurrentShotEnemyEnemyCollisionCount",
            "m_CurrentShotBankShotReady",
            "m_CurrentShotLaunchPower",
        ):
            self.assertNotIn(old_shot_member, game)

    def test_run_state_is_owned_and_mutated_by_run_controller(self) -> None:
        game = source("Game.h")
        run = source("RunController.h") + source("RunController.cpp")

        self.assertIn("RunController m_RunController", game)
        self.assertNotIn("PlayerRunStatus m_PlayerRunStatus", game)
        self.assertNotIn("PlayerDeck m_PlayerDeck", game)
        self.assertNotIn("RunProgressController m_RunProgress", game)
        self.assertIn("PlayerRunStatus m_Status", run)
        self.assertIn("PlayerDeck m_Deck", run)
        self.assertIn("std::unique_ptr<RunProgressController> m_Progress", run)
        self.assertNotIn('#include "RunProgressController.h"', source("RunController.h"))
        self.assertIn("bool RunController::RestHeal()", run)
        self.assertIn("bool RunController::BuyShopBall", run)
        self.assertIn("bool RunController::BuyRelic", run)
        self.assertIn("bool RunController::CollectStageReward", run)

    def test_autoplay_state_and_decisions_are_owned_by_balance_auto_player(self) -> None:
        game = source("Game.h")
        autoplay = source("BalanceAutoPlayer.h") + source("GameAutoPlay.cpp")

        self.assertIn("BalanceAutoPlayer m_BalanceAutoPlayer", game)
        self.assertNotIn("bool UpdateBalanceAutoPlay();", game)
        self.assertNotIn("void LoadBalanceAutoPlayConfig(", game)
        for old_member in (
            "m_BalanceAutoPlayEnabled",
            "m_AutoRestartAfterGameOver",
            "m_BalanceAutoDecisionFrame",
            "m_BalanceAutoRandom",
            "m_BalanceAutoPendingBallAdjustments",
        ):
            self.assertNotIn(old_member, game)

        for decision in (
            "bool BalanceAutoPlayer::Update(Game& game)",
            "void BalanceAutoPlayer::SelectBall(Game& game)",
            "bool BalanceAutoPlayer::FireShot(Game& game)",
            "void BalanceAutoPlayer::ApplyReward(Game& game)",
        ):
            self.assertIn(decision, autoplay)

        self.assertIn("game.ApplyBalanceAutoBallSelection", autoplay)
        self.assertIn("game.MarkBalanceAutoRewardChosen", autoplay)

    def test_balance_system_state_is_owned_by_controllers(self) -> None:
        game = source("Game.h")
        dynamic = source("DynamicBalanceController.h") + source(
            "DynamicBalanceController.cpp"
        )
        validation = source("BalanceValidationController.h") + source(
            "BalanceValidationController.cpp"
        )

        self.assertIn("DynamicBalanceController m_DynamicBalanceController", game)
        self.assertIn(
            "BalanceValidationController m_BalanceValidationController", game
        )
        for old_member in (
            "m_DynamicBalanceEnabled",
            "m_DynamicBalanceLevel",
            "m_DynamicBalanceStageShots",
            "m_BalanceValidationEnabled",
            "m_BalanceValidationSeeds",
            "m_BalanceValidationVariants",
        ):
            self.assertNotIn(old_member, game)

        self.assertIn(
            "int DynamicBalanceController::CalculateProgressionHpModifier(",
            dynamic,
        )
        self.assertIn(
            "int DynamicBalanceController::CalculateProgressionAttackModifier(",
            dynamic,
        )
        for retired_event in (
            "DynamicBalanceController::OnRunStarted",
            "DynamicBalanceController::OnBattleStarted",
            "DynamicBalanceController::OnShotStarted",
            "DynamicBalanceController::OnHit",
            "DynamicBalanceController::OnShotFinished",
            "DynamicBalanceController::OnBattleFinished",
        ):
            self.assertNotIn(retired_event, dynamic)
        self.assertIn(
            "BalanceValidationRun BalanceValidationController::OnRunStarted(",
            validation,
        )

    def test_debug_state_and_workflows_are_owned_by_debug_controller(self) -> None:
        game = source("Game.h")
        debug = (
            source("GameDebugController.h")
            + source("GameDebugMode.cpp")
            + source("GameStageEditor.cpp")
            + source("Game.cpp")
        )

        self.assertIn("std::unique_ptr<GameDebugController> m_DebugController", game)
        self.assertNotIn('#include "GameDebugController.h"', game)
        for old_member in (
            "bool m_DebugMode",
            "bool m_DebugEditorOpen",
            "DebugBattleSetup m_DebugSetup",
            "std::vector<EnemyData> m_DebugEnemyCatalog",
            "StageLayoutEditor m_StageEditor",
            "DebugCombatForecastSnapshot m_DebugCombatForecast",
            "std::deque<DebugPlayerDamageRecord> m_DebugPlayerDamageHistory",
        ):
            self.assertNotIn(old_member, game)

        for workflow in (
            "void GameDebugController::Open(Game& game)",
            "bool GameDebugController::StartBattle(Game& game)",
            "void GameDebugController::FinishBattle(Game& game, bool victory)",
            "void GameDebugController::SavePreset()",
            "bool GameDebugController::LoadPreset()",
            "void GameDebugController::RefreshCombatForecast(Game& game)",
            "void GameDebugController::RecordPlayerDamage(",
            "void GameDebugController::DrawDiagnostics(Game& game)",
            "void GameDebugController::DrawStageEditor(Game&)",
        ):
            self.assertIn(workflow, debug)

    def test_player_facing_draw_workflows_are_owned_by_presentation(self) -> None:
        game_header = source("Game.h")
        game_source = source("Game.cpp")
        presentation_header = source("GamePresentation.h")
        presentation = presentation_header + game_source

        self.assertIn("std::unique_ptr<GamePresentation> m_GamePresentation", game_header)
        for workflow in (
            "PausePresentationIntent GamePresentation::DrawPause(",
            "BallSelectionPresentationIntent GamePresentation::DrawBallSelection(",
            "ClearRewardPresentationIntent GamePresentation::DrawClearReward(",
        ):
            self.assertIn(workflow, presentation)
        self.assertNotIn("friend class GamePresentation", game_header)
        self.assertNotIn("game.m_", source("GamePresentation.cpp"))
        self.assertIn("PausePresentationModel", presentation_header)
        self.assertIn("BallSelectionPresentationModel", presentation_header)
        self.assertIn("ClearRewardPresentationModel", presentation_header)
        for old_workflow in (
            "void Game::DrawPauseUI()",
            "void Game::DrawBallSelectionUI()",
            "void Game::DrawClearRewardUI()",
        ):
            self.assertNotIn(old_workflow, game_source)

    def test_boss_shot_search_is_owned_by_planner(self) -> None:
        game = source("Game.h")
        planner = source("BossShotPlanner.h") + source("GameBossAI.cpp")

        self.assertIn("std::unique_ptr<BossShotPlanner> m_BossShotPlanner", game)
        self.assertNotIn('#include "BossShotPlanner.h"', game)
        self.assertNotIn("friend class BossShotPlanner", game)
        self.assertNotIn("m_BossShotCache", game)
        self.assertNotIn("m_BossShotCacheKey", game)
        self.assertIn("json BossShotPlanner::Evaluate(Game& game)", planner)
        self.assertIn("bool BossShotPlanner::Fire(Game& game", planner)

    def test_automated_shot_sources_share_the_validated_game_boundary(self) -> None:
        game = source("Game.h") + source("GameBossAI.cpp")
        autoplay = source("GameAutoPlay.cpp")
        mcp = source("GameMcpBridge.cpp")

        self.assertIn("bool Game::TryFireAutomatedShot(", game)
        self.assertIn("GetBattleState() != BattleState::AimingDirection", game)
        self.assertIn("!AreAllBallsStopped()", game)
        self.assertIn("game.TryFireAutomatedShot(", autoplay)
        self.assertIn("game.TryFireAutomatedShot(", mcp)
        self.assertNotIn("friend class BalanceAutoPlayer", source("Game.h"))

    def test_save_uses_typed_owner_snapshots(self) -> None:
        game = source("Game.h")
        deck = source("PlayerDeck.h")
        selector = source("StageSelector.h")
        save = source("GameSaveManager.cpp")
        bridge = source("GameSaveBridge.cpp")
        boundary = source("GameRunSaveState.h")

        self.assertIn("PlayerDeckSnapshot CaptureSnapshot() const", deck)
        self.assertIn("bool RestoreSnapshot(PlayerDeckSnapshot snapshot)", deck)
        self.assertIn("StageSelectorSnapshot CaptureSnapshot() const", selector)
        self.assertNotIn("friend class GameSaveManager", game + deck + selector)
        self.assertNotIn("game.m_", save)
        self.assertIn("GameRunSaveSnapshot", boundary)
        self.assertIn("GameRunRestoreRequest", boundary)
        self.assertIn("game.CaptureRunSaveSnapshot()", save)
        self.assertIn("game.RestoreRunSaveSnapshot(", save)
        self.assertIn("GameRunSaveSnapshot Game::CaptureRunSaveSnapshot() const", bridge)
        self.assertIn("void Game::RestoreRunSaveSnapshot(", bridge)
        self.assertIn("m_RunController.Deck().CaptureSnapshot()", bridge)
        self.assertIn("m_RunController.Deck().RestoreSnapshot(", bridge)

    def test_non_battle_scenes_do_not_depend_on_game_header(self) -> None:
        for scene_name in (
            "RestSiteScene.cpp",
            "ShopScene.cpp",
            "TitleScene.cpp",
            "ResultScene.cpp",
            "StageSelectScene.cpp",
        ):
            scene = source(scene_name)
            self.assertNotIn('#include "Game.h"', scene)
            self.assertNotIn("Game::GetInstance()", scene)

        view = source("GameView.h")
        self.assertIn("RestSiteViewSnapshot", view)
        self.assertIn("ShopViewSnapshot", view)
        self.assertIn("TitleViewSnapshot", view)
        self.assertNotIn('#include "Game.h"', view)

    def test_route_sources_share_one_validated_command(self) -> None:
        stage_select = source("StageSelectScene.cpp")
        autoplay = source("GameAutoPlay.cpp")
        mcp = source("GameMcpBridge.cpp")
        boundary = source("GameSceneAccess.cpp")

        self.assertIn("RunRouteCommands::Choose(", stage_select)
        self.assertIn('stageSelect->ChooseRoute(selectedRoute, "autoplay")', autoplay)
        self.assertIn('stageSelect->ChooseRoute(routeIndex, "mcp")', mcp)
        self.assertIn("bool RunRouteCommands::Choose(", boundary)
        self.assertIn("game.ChooseMapNode(nodeId)", boundary)
        self.assertIn('game.RecordBalanceEvent(\n        "route_choice"', boundary)
        self.assertIn("game.StartNextBattle(StageType::Normal)", boundary)
        self.assertIn("game.ChangeScene(SceneType::Shop)", boundary)


if __name__ == "__main__":
    unittest.main()
