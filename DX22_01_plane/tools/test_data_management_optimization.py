from __future__ import annotations

import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_source(name: str) -> str:
    return (ROOT / name).read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    depth = 0
    # Ignore braces in comments and literals; function order is not a boundary.
    tokens = r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]'
    for match in re.finditer(tokens, source[start:], flags=re.DOTALL):
        if match.group() == "{":
            depth += 1
        elif match.group() == "}":
            depth -= 1
            if depth == 0:
                return source[start:start + match.end()]
    raise ValueError(f"Unterminated function: {signature}")


class DataManagementOptimizationTests(unittest.TestCase):
    def test_balance_events_are_batched_until_a_shot_boundary(self) -> None:
        events = read_source("BalanceLoggerEvents.cpp")
        player_damage = function_body(
            events,
            "void BalanceLogger::RecordPlayerDamage",
        )
        generic_event = function_body(
            events,
            "void BalanceLogger::RecordEvent",
        )
        begin_shot = function_body(
            read_source("BalanceLoggerShot.cpp"),
            "void BalanceLogger::BeginShot",
        )

        self.assertIn("m_SavePending = true;", player_damage)
        self.assertIn("m_SavePending = true;", generic_event)
        self.assertNotRegex(player_damage, r"\bSave\s*\(")
        self.assertNotRegex(generic_event, r"\bSave\s*\(")
        self.assertRegex(begin_shot, r"\bSave\s*\(")

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
        source = read_source("BallPhysicsWorld.cpp")
        resolve = source[source.index(
            "ContinuousBallStepper::Result BallPhysicsWorld::Step("
        ):]

        self.assertEqual(resolve.count("GetComponents<BallComponent>()"), 1)
        self.assertEqual(resolve.count("GetComponents<Pocket>()"), 1)
        before_steps = resolve[:resolve.index("ContinuousBallStepper::Step(world)")]
        self.assertIn("GetComponents<BallComponent>()", before_steps)
        self.assertIn("GetComponents<Pocket>()", before_steps)

    def test_status_normalization_has_one_definition_per_data_type(self) -> None:
        sources = "\n".join(
            read_source(name)
            for name in (
                "BallStatus.h",
                "PlayerRunStatus.h",
                "BallStatusComponent.h",
                "Game.cpp",
                "GamePlayerBallLifecycle.cpp",
                "GameRestActions.cpp",
                "GameRewardOperations.cpp",
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
