from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def source(name: str) -> str:
    return (ROOT / name).read_text(encoding="utf-8-sig")


class GameResponsibilitySplitContractTests(unittest.TestCase):
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
        self.assertIn("RunProgressController m_Progress", run)
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

        self.assertIn("GameDebugController m_DebugController", game)
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
        presentation = source("GamePresentation.h") + game_source

        self.assertIn("std::unique_ptr<GamePresentation> m_GamePresentation", game_header)
        for workflow in (
            "void GamePresentation::DrawPause(Game& game)",
            "void GamePresentation::DrawBallSelection(Game& game)",
            "void GamePresentation::DrawClearReward(Game& game)",
        ):
            self.assertIn(workflow, presentation)
        for old_workflow in (
            "void Game::DrawPauseUI()",
            "void Game::DrawBallSelectionUI()",
            "void Game::DrawClearRewardUI()",
        ):
            self.assertNotIn(old_workflow, game_source)

    def test_boss_shot_search_is_owned_by_planner(self) -> None:
        game = source("Game.h")
        planner = source("BossShotPlanner.h") + source("GameBossAI.cpp")

        self.assertIn("BossShotPlanner m_BossShotPlanner", game)
        self.assertNotIn("m_BossShotCache", game)
        self.assertNotIn("m_BossShotCacheKey", game)
        self.assertIn("json BossShotPlanner::Evaluate(Game& game)", planner)
        self.assertIn("bool BossShotPlanner::Fire(Game& game", planner)


if __name__ == "__main__":
    unittest.main()
