from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from compare_paired_balance_runs import build_report


class PairedBalanceComparisonTests(unittest.TestCase):
    @staticmethod
    def _run(
        seed: int,
        variant: str,
        reached: int,
        build_profile: str = "standard",
    ) -> dict:
        return {
            "controller": {
                "type": "mcp",
                "profile": "intermediate",
                "build_profile": build_profile,
            },
            "run_context": {
                "randomness": {"run_seed": seed},
                "validation": {
                    "enabled": True,
                    "experiment_id": "paired_dda_5x2",
                    "variant_id": variant,
                    "dynamic_balance_forced_off": variant == "dda_off",
                },
            },
            "run_result": {
                "reached_stage_index": reached,
                "cleared_stage_count": reached - 1,
                "remaining_hp_ratio": 0.0,
            },
            "stages": [{
                "stage_type": "boss" if reached >= 10 else "normal",
                "stage_result": {"total_shots": reached * 4},
            }],
        }

    def test_same_five_seeds_are_compared_between_variants(self) -> None:
        config = {
            "experiment_id": "paired_dda_5x2",
            "random_seeds": [11, 12, 13, 14, 15],
            "variants": [
                {"id": "dda_off", "disable_dynamic_balance": True},
                {"id": "dda_on", "disable_dynamic_balance": False},
            ],
            "minimum_runs_per_variant": 5,
            "minimum_paired_seeds": 5,
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for variant in ("dda_off", "dda_on"):
                for seed in config["random_seeds"]:
                    reached = 5 if variant == "dda_off" else 7
                    path = root / f"run_{variant}_{seed}.json"
                    path.write_text(
                        json.dumps(self._run(seed, variant, reached)),
                        encoding="utf-8",
                    )

            report = build_report(root, config)

        self.assertEqual(report["status"], "passed")
        self.assertEqual(report["paired_seeds"], config["random_seeds"])
        self.assertEqual(
            report["comparison"][
                "delta_comparison_minus_baseline"
            ]["reached_progress"],
            2.0,
        )

    def test_build_filter_prevents_cross_build_seed_duplicates(self) -> None:
        config = {
            "experiment_id": "paired_dda_5x2",
            "random_seeds": [11],
            "variants": [
                {"id": "dda_off", "disable_dynamic_balance": True},
                {"id": "dda_on", "disable_dynamic_balance": False},
            ],
            "minimum_runs_per_variant": 1,
            "minimum_paired_seeds": 1,
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for build_profile in ("standard", "heavy"):
                for variant in ("dda_off", "dda_on"):
                    path = root / f"run_{build_profile}_{variant}.json"
                    path.write_text(
                        json.dumps(self._run(
                            11,
                            variant,
                            5,
                            build_profile,
                        )),
                        encoding="utf-8",
                    )

            report = build_report(
                root,
                config,
                controller_profile="intermediate",
                build_profile="heavy",
            )

        self.assertEqual(report["status"], "passed")
        self.assertEqual(report["paired_seeds"], [11])
        self.assertEqual(report["cohort"]["build_profile"], "heavy")


if __name__ == "__main__":
    unittest.main()
