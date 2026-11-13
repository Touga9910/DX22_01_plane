from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def source(name: str) -> str:
    return (ROOT / name).read_text(encoding="utf-8-sig")


class McpBallInformationContractTests(unittest.TestCase):
    def test_current_status_reuses_canonical_serializer_and_keeps_aliases(self) -> None:
        bridge = source("GameMcpBridge.cpp")
        helper = bridge[
            bridge.index("nlohmann::json PlayerBallStatusToMcpJson"):
            bridge.index("nlohmann::json BallDataToJson")
        ]
        self.assertIn("WritePlayerBallStatus(status)", helper)
        for alias in (
            "split", "pierce", "anchor", "refractAfterPierce",
            "heavyDistanceStacks", "lightweight", "reflectionSetup",
            "returnToShotStart",
        ):
            self.assertIn(f'value["{alias}"]', helper)
        self.assertIn(
            '{ "status", PlayerBallStatusToMcpJson(ball.status) }',
            bridge,
        )

    def test_canonical_serializer_contains_current_special_parameters(self) -> None:
        serializer = source("BallStatusJson.h")
        for field in (
            "heavyFinisherDamagePerCollision",
            "heavyCollisionConsumeAmount",
            "lightweightSpeedLossReduction",
            "cushionStackGenerateAmount",
            "cushionBounceAttackBonus",
            "pierceFinisherBaseBonus",
            "pierceFinisherMultiTargetBonus",
            "anchorEnemyStackGenerate",
            "anchorFinisherStackConsume",
            "anchorFinisherAoeThreshold",
            "anchorFinisherAoeRadius",
        ):
            self.assertIn(f'{{ "{field}", status.{field} }}', serializer)

    def test_anchor_and_prepared_effect_state_is_target_addressable(self) -> None:
        bridge = source("GameMcpBridge.cpp")
        for field in (
            '"target_id", targetId',
            '"anchor_stacks", enemy->GetAnchorStacks()',
            '"anchor_conversion_progress"',
            '"anchor_next_grant_bonus"',
            '"origin_return_pending"',
            '"heavy_pending_enemy_collision_multiplier"',
            '"pending_lightweight_knockback_multiplier"',
            '"next_shot_effects"',
            '"shared_stack_count"',
            '"pending_setup_bonus"',
        ):
            self.assertIn(field, bridge)

    def test_new_ability_events_exist_in_runtime_and_collector(self) -> None:
        shot_start = source("GameShotStart.cpp")
        shot_end = source("GameShotEnd.cpp")
        collector = source("tools/game_mcp/collect_fixed_balance_runs.py")
        self.assertIn('"origin_anchor_return_armed"', shot_start)
        self.assertIn('"reflection_setup_armed"', shot_end)
        self.assertIn('"origin_anchor_return_armed"', collector)
        self.assertIn('"reflection_setup_armed"', collector)


if __name__ == "__main__":
    unittest.main()
