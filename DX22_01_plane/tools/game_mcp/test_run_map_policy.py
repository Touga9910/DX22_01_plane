import unittest
from pathlib import Path

from bridge_store import GameBridgeError
from build_profiles import load_build_profiles
from server import resolve_mcp_stage_choice


class RunMapPolicyTests(unittest.TestCase):
    def state(self):
        return {
            "run_map": {"version": 1},
            "player": {"current_hp": 10, "max_hp": 50, "money": 0},
            "route_options": [
                {"route_index": 0, "node_id": 6, "destination": "shop"},
                {"route_index": 1, "node_id": 7, "destination": "shop"},
                {"route_index": 2, "node_id": 8, "destination": "rest"},
            ],
            "rest_heal": {"available": True},
            "deck_rule": {"can_remove": False},
        }

    def test_explicit_path_survives_low_hp_and_unaffordable_shop(self):
        result = resolve_mcp_stage_choice(self.state(), ["hp_recovery"], {}, 1)
        self.assertEqual(result["effective_route_index"], 1)
        self.assertEqual(result["node_id"], 7)
        self.assertFalse(result["overridden"])

    def test_missing_index_retains_automatic_recovery(self):
        result = resolve_mcp_stage_choice(self.state(), None, {})
        self.assertEqual(result["effective_route_index"], 2)

    def test_invalid_map_index_is_rejected(self):
        with self.assertRaises(GameBridgeError):
            resolve_mcp_stage_choice(self.state(), None, {}, 99)

    def test_explicit_route_is_still_checked_by_integrated_ai(self):
        profiles, _ = load_build_profiles(
            Path(__file__).resolve().parent / "build_profiles.json"
        )
        state = self.state()
        state["deck_balls"] = []
        result = resolve_mcp_stage_choice(
            state,
            None,
            profiles["standard"],
            0,
            profiles,
            "standard",
        )
        self.assertEqual(result["effective_route_index"], 2)
        self.assertTrue(result["overridden"])
        self.assertIn("route_evaluation", result)


if __name__ == "__main__":
    unittest.main()
