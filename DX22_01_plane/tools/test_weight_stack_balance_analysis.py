from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).with_name("analyze_weight_stack_balance.py")
SPEC = importlib.util.spec_from_file_location("weight_stack_analysis", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
analysis = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(analysis)


class WeightStackBalanceAnalysisTests(unittest.TestCase):
    def test_each_enemy_is_floored_before_summing(self) -> None:
        gain = analysis.calculate_gain([19.9, 19.9], 20.0)
        self.assertEqual(gain["per_enemy"], [0, 0])
        self.assertEqual(gain["distance_uncapped"], 0)

    def test_candidate_distances_and_turn_limit(self) -> None:
        distances = [70.0, 43.0, 19.0]
        self.assertEqual(analysis.calculate_gain(distances, 5.0)["distance_uncapped"], 25)
        self.assertEqual(analysis.calculate_gain(distances, 20.0)["distance_uncapped"], 5)
        self.assertEqual(analysis.calculate_gain(distances, 40.0)["distance_uncapped"], 2)
        self.assertEqual(analysis.calculate_gain(distances, 60.0)["distance_uncapped"], 1)
        self.assertEqual(analysis.calculate_gain(distances, 5.0)["actual"], 10)
        self.assertEqual(
            analysis.calculate_gain(distances, 5.0)["discarded_by_turn_limit"],
            15,
        )

    def test_lightweight_gain_remains_separate(self) -> None:
        gain = analysis.calculate_gain([39.0], 20.0, lightweight_gain=3)
        self.assertEqual(gain["distance_uncapped"], 1)
        self.assertEqual(gain["lightweight_uncapped"], 3)
        self.assertEqual(gain["combined_uncapped"], 4)
        self.assertEqual(gain["actual"], 4)

    def test_zero_distance_and_storage_discard_contract(self) -> None:
        gain = analysis.calculate_gain([0.0, -3.0], 20.0)
        self.assertEqual(gain["distance_uncapped"], 0)
        self.assertEqual(gain["discarded_by_storage_limit"], 0)


if __name__ == "__main__":
    unittest.main()
