import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class TrajectoryPreviewResponsibilityContract(unittest.TestCase):
    def test_player_display_uses_lightweight_preview(self):
        player = (ROOT / "PlayerBall.cpp").read_text(
            encoding="utf-8", errors="ignore"
        )
        start = player.index("void PlayerBall::GeneratePreTrajectory")
        end = player.index("void PlayerBall::InitTrajectoryVisualModel", start)
        body = player[start:end]
        self.assertIn("GameShotPrediction::Preview", body)
        self.assertNotIn("GameShotPrediction::Predict", body)

    def test_detailed_prediction_remains_for_boss_ai(self):
        boss = (ROOT / "GameBossAI.cpp").read_text(
            encoding="utf-8", errors="ignore"
        )
        bridge = (ROOT / "GameBattleBridge.cpp").read_text(
            encoding="utf-8", errors="ignore"
        )
        self.assertIn("GameShotPrediction::Predict", boss)
        self.assertIn("BallShotPrediction::Predict", bridge)
        self.assertIn("BallTrajectoryPreview::Predict", bridge)

    def test_lightweight_predictor_has_no_combat_result_pipeline(self):
        preview = (ROOT / "BallTrajectoryPreview.cpp").read_text(
            encoding="utf-8", errors="ignore"
        )
        self.assertIn("MaximumPreviewTicks", preview)
        self.assertIn("reachedBlockingContact", preview)
        self.assertNotIn("result.damage", preview)
        self.assertNotIn("HandleDefeat", preview)
        self.assertNotIn("ShotRelicRules", preview)

    def test_cache_uses_lightweight_world_signature(self):
        player = (ROOT / "PlayerBall.cpp").read_text(
            encoding="utf-8", errors="ignore"
        )
        self.assertIn("GameShotPrediction::PreviewWorldKey", player)
        self.assertIn("RecordTrajectoryPreviewCacheReuse", player)
        self.assertNotIn(
            "m_LastPreviewWorldKey != GameShotPrediction::WorldKey()", player
        )


if __name__ == "__main__":
    unittest.main()
