from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_source(name: str) -> str:
    return (ROOT / name).read_text(encoding="utf-8")


def function_body(source: str, signature: str, next_signature: str) -> str:
    start = source.index(signature)
    end = source.index(next_signature, start)
    return source[start:end]


class DataManagementOptimizationTests(unittest.TestCase):
    def test_balance_events_are_batched_until_a_shot_boundary(self) -> None:
        source = read_source("BalanceLogger.cpp")
        player_damage = function_body(
            source,
            "void BalanceLogger::RecordPlayerDamage",
            "void BalanceLogger::RecordEvent",
        )
        generic_event = function_body(
            source,
            "void BalanceLogger::RecordEvent",
            "void BalanceLogger::EndShot",
        )
        begin_shot = function_body(
            source,
            "void BalanceLogger::BeginShot",
            "void BalanceLogger::RecordDamageCollision",
        )

        self.assertIn("m_SavePending = true;", player_damage)
        self.assertIn("m_SavePending = true;", generic_event)
        self.assertNotIn("\n\tSave();", player_damage)
        self.assertNotIn("\n\tSave();", generic_event)
        self.assertIn("\n\tSave();", begin_shot)

    def test_mcp_file_polling_and_state_publish_have_separate_rates(self) -> None:
        with (ROOT / "assets/data/game_mcp_bridge.json").open(
            "r", encoding="utf-8"
        ) as source:
            config = json.load(source)

        self.assertGreaterEqual(config["state_publish_interval_frames"], 60)
        self.assertGreaterEqual(config["command_poll_interval_frames"], 1)
        self.assertLess(
            config["command_poll_interval_frames"],
            config["state_publish_interval_frames"],
        )

    def test_collision_lists_are_not_rebuilt_inside_substeps(self) -> None:
        source = read_source("BallCollisionComponent.cpp")
        resolve = function_body(
            source,
            "void BallCollisionComponent::ResolveMovementAndCollisions",
            "void BallCollisionComponent::ResetShotAbilityState",
        )

        self.assertEqual(resolve.count("GetComponents<BallComponent>()"), 1)
        self.assertEqual(resolve.count("GetComponents<Pocket>()"), 1)

    def test_status_normalization_has_one_definition_per_data_type(self) -> None:
        sources = "\n".join(
            read_source(name)
            for name in (
                "BallStatus.h",
                "PlayerRunStatus.h",
                "BallStatusComponent.h",
                "Game.cpp",
                "GameProgression.cpp",
                "GameBalanceConfig.cpp",
                "PlayerBallDataLoader.cpp",
                "PlayerDeck.cpp",
            )
        )

        self.assertEqual(sources.count("BallStatus NormalizeBallStatus("), 1)
        self.assertEqual(
            sources.count("PlayerRunStatus NormalizePlayerRunStatus("), 1
        )

    def test_trajectory_preview_is_throttled_while_aiming(self) -> None:
        header = read_source("PlayerBall.h")
        source = read_source("PlayerBall.cpp")

        self.assertIn("m_PreviewRefreshFramesRemaining", header)
        self.assertIn("kPreviewRefreshCooldownFrames", source)
        self.assertIn("else if (previewChanged)", source)


if __name__ == "__main__":
    unittest.main()
