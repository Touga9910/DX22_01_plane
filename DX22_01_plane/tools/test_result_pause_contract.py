from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def source(name: str) -> str:
    return (ROOT / name).read_text(encoding="utf-8-sig")


class ResultPauseContractTests(unittest.TestCase):
    def test_result_contains_requested_summary_and_actions(self) -> None:
        result = source("ResultScene.cpp")

        for label in (
            "通常ルート通過",
            "総戦闘数",
            "ボス戦績",
            "最終ボス",
            "ショット数",
            "1ターン最大ダメージ",
            "プレイ時間",
            "獲得ボール",
            "獲得レリック",
            "ラン分析を見る",
            "もう一度",
            "タイトルへ",
        ):
            self.assertIn(label, result)
        self.assertIn("StartNewRun()", result)
        self.assertIn("DrawAnalysis()", result)

    def test_finite_run_endpoint_contract(self) -> None:
        game_types = source("GameTypes.h")
        game = source("Game.cpp")
        run_progress = source("RunProgressController.h")
        routes = source("StageSelectScene.cpp")
        save_manager = source("GameSaveManager.cpp")

        for phase in (
            "NormalRoute",
            "BossPreparation",
            "FinalBossReady",
            "FinalBoss",
            "Completed",
        ):
            self.assertIn(phase, game_types)
        self.assertIn("NormalRouteAreaGoal = 15", run_progress)
        self.assertIn("RunProgressController m_RunProgress", source("Game.h"))
        self.assertIn("CompleteNormalRouteArea", game)
        self.assertIn("CompleteFinalBossRun", game)
        self.assertIn("m_BalanceValidationEnduranceMode", game)
        self.assertIn("GameSaveManager::Remove()", game)
        self.assertIn("GetRunMap().Available()", routes)
        self.assertIn("ChooseMapNode(nodeId)", routes)
        self.assertIn('"run_map"', save_manager)
        self.assertIn("RunMap::Restore", save_manager)
        self.assertIn("StageRouteType::FinalBoss", routes)
        self.assertIn('"area_progress"', save_manager)
        self.assertIn('"run_phase"', save_manager)
        self.assertIn('"run_endpoint_save_migrated"', save_manager)

    def test_player_run_statistics_are_collected_and_saved(self) -> None:
        snapshot = source("RunResultSnapshot.h")
        tracker = source("RunStatisticsTracker.cpp")
        save_manager = source("GameSaveManager.cpp")

        for field in (
            "reachedFloor",
            "totalShots",
            "totalDamage",
            "maximumTurnDamage",
            "damageTaken",
            "activeFrames",
            "acquiredBallIds",
            "ballShotCounts",
        ):
            self.assertIn(field, snapshot)
        self.assertIn("RunStatisticsTracker::BeginShot", tracker)
        self.assertIn("RunStatisticsTracker::RecordEnemyDamage", tracker)
        self.assertIn("RunStatisticsTracker::CreateSnapshot", tracker)
        self.assertIn('"run_statistics"', save_manager)

    def test_escape_opens_pause_instead_of_closing_the_window(self) -> None:
        application = source("Application.cpp")
        game = source("Game.cpp")

        self.assertNotIn("PostMessage(hWnd, WM_CLOSE, wParam, lParam)", application)
        self.assertIn("Input::GetKeyTrigger(VK_ESCAPE)", game)
        self.assertIn("DrawPauseUI()", game)
        self.assertIn("SaveAndReturnToTitle()", game)
        self.assertIn("ランのセーブデータは1個だけです", game)

    def test_pause_dimmer_is_behind_the_pause_window(self) -> None:
        game = source("Game.cpp")

        self.assertNotIn("GetForegroundDrawList()->AddRectFilled", game)
        self.assertIn('ImGui::Begin("##PauseDimmer"', game)
        self.assertLess(
            game.index('ImGui::Begin("##PauseDimmer"'),
            game.index('ImGui::Begin("ポーズ"'),
        )

    def test_pause_settings_are_persisted_and_applied(self) -> None:
        settings = source("SettingsManager.cpp")
        presentation = source("GamePresentation.cpp")
        application = source("Application.cpp")

        for key in (
            '"bgm_volume"',
            '"se_volume"',
            '"vibration_enabled"',
            '"screen_flash_enabled"',
            '"camera_shake_enabled"',
            '"fullscreen"',
            '"resolution_index"',
        ):
            self.assertIn(key, settings)
        self.assertIn("SetDisplayMode", application)
        self.assertIn("IsVibrationEnabled", presentation)
        self.assertIn("IsScreenFlashEnabled", presentation)
        self.assertIn("IsCameraShakeEnabled", presentation)

    def test_analysis_is_a_pure_snapshot_transformation(self) -> None:
        analysis_header = source("PlayerRunAnalysis.h")
        analysis = source("PlayerRunAnalysis.cpp")
        result = source("ResultScene.cpp")

        self.assertIn(
            "PlayerRunAnalysis AnalyzeRun(const RunResultSnapshot& result)",
            analysis_header,
        )
        self.assertNotIn("Game::GetInstance", analysis)
        self.assertNotIn("ifstream", analysis)
        self.assertIn("m_Analysis = AnalyzeRun(m_Result)", result)

    def test_common_menu_and_game_events_are_reused(self) -> None:
        menu = source("MenuSelection.cpp")
        events = source("GameEvent.h")
        progression = source("GameProgression.cpp")

        self.assertIn("UpdateVertical", menu)
        self.assertIn("XINPUT_UP", menu)
        self.assertIn("using GameEvent = std::variant", events)
        self.assertIn("PublishGameEvent", progression)
        for scene in (
            "TitleScene.cpp",
            "ResultScene.cpp",
            "ShopScene.cpp",
            "RestSiteScene.cpp",
        ):
            self.assertIn("m_Menu.UpdateVertical", source(scene))


if __name__ == "__main__":
    unittest.main()
