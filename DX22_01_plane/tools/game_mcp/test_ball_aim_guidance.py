from __future__ import annotations

import json
import unittest
from pathlib import Path

from ball_aim_guidance import BALL_AIM_GUIDANCE, attach_ball_aim_guidance


class BallAimGuidanceTests(unittest.TestCase):
    def test_every_runtime_ball_has_actionable_guidance(self) -> None:
        catalog_path = Path(__file__).resolve().parents[2] / "assets/data/player_status.json"
        catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
        ids = {ball["id"] for ball in catalog["balls"]}
        self.assertEqual(ids, set(BALL_AIM_GUIDANCE))
        for guidance in BALL_AIM_GUIDANCE.values():
            for field in ("role", "target_pattern", "shot_path", "follow_up"):
                self.assertTrue(guidance[field])

    def test_live_resource_counts_are_attached_to_each_ball_collection(self) -> None:
        ids = (
            "player_chain_impact", "player_trace_driver",
            "player_ricochet_finisher", "player_anchor_finisher",
        )
        state = {
            collection: [{"definition_id": definition_id} for definition_id in ids]
            for collection in ("offered_balls", "deck_balls", "catalog_balls")
        }
        state["ball_synergies"] = {
            "heavy_collision_count": 2,
            "pierce_traces": [{"id": 1}],
            "player_anchor_stacks": 1,
            "enemy_anchor_stacks": [{"stacks": 2}],
        }
        state["table"] = {"cushions": [{"stack_count": 2}, {"stack_count": 1}]}

        attach_ball_aim_guidance(state)

        expected = (2, 1, 3, 3)
        for collection in ("offered_balls", "deck_balls", "catalog_balls"):
            self.assertEqual(
                expected,
                tuple(ball["aim_guidance"]["current_resource"]["count"]
                      for ball in state[collection]),
            )
            self.assertTrue(all(
                ball["aim_guidance"]["current_resource"]["available"]
                for ball in state[collection]
            ))


if __name__ == "__main__":
    unittest.main()
