from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from build_comparison_jobs import BuildComparisonJobManager


class FakeProcess:
    def __init__(self) -> None:
        self.return_code = None

    def poll(self):
        return self.return_code


class BuildComparisonJobManagerTests(unittest.TestCase):
    def test_start_is_non_blocking_and_forwards_collector_arguments(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            manager = BuildComparisonJobManager(
                Path(directory), "http://127.0.0.1:8765/mcp"
            )
            process = FakeProcess()
            with patch("build_comparison_jobs.subprocess.Popen", return_value=process) as popen:
                result = manager.start(
                    [1001, 1002],
                    ["standard", "heavy"],
                    "intermediate",
                    "fixed",
                    2,
                    False,
                )

            self.assertTrue(result["ok"])
            self.assertTrue(result["running"])
            command = popen.call_args.args[0]
            self.assertIn("--comparison-suite", command)
            self.assertEqual(command.count("--comparison-seed"), 2)
            self.assertEqual(command.count("--comparison-build"), 2)
            self.assertIn("collect_fixed_balance_runs.py", command[1])

    def test_rejects_non_paired_or_dynamic_requests(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            manager = BuildComparisonJobManager(
                Path(directory), "http://127.0.0.1:8765/mcp"
            )
            with self.assertRaisesRegex(ValueError, "dynamic_balance"):
                manager.start(
                    [1001], ["standard"], "intermediate", "fixed", 1, True
                )

    def test_resume_reuses_matching_output_directory(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            manager = BuildComparisonJobManager(
                Path(directory), "http://127.0.0.1:8765/mcp"
            )
            resume = manager.output_root / "comparison_failed"
            resume.mkdir(parents=True)
            (resume / "comparison_request.json").write_text(
                json.dumps({
                    "seeds": [1001, 1002],
                    "build_profiles": ["standard", "bounce"],
                    "player_level_requested": "intermediate",
                    "validation_variant": "fixed",
                    "valid_runs_per_build": 2,
                    "dynamic_balance": False,
                }),
                encoding="utf-8",
            )
            (resume / "all_runs.jsonl").write_text(
                '{"run_seed":1001,"build_profile":"standard"}\n',
                encoding="utf-8",
            )
            process = FakeProcess()
            with patch(
                "build_comparison_jobs.subprocess.Popen", return_value=process
            ) as popen:
                result = manager.start(
                    [1001, 1002],
                    ["standard", "bounce"],
                    "intermediate",
                    "fixed",
                    2,
                    False,
                    str(resume),
                )
            self.assertEqual(
                Path(result["output_paths"]["output_directory"]),
                resume.resolve(),
            )
            command = popen.call_args.args[0]
            output_index = command.index("--output-directory") + 1
            self.assertEqual(Path(command[output_index]), resume.resolve())
            self.assertIn(
                '"run_seed":1001',
                (resume / "all_runs.jsonl").read_text(encoding="utf-8"),
            )

    def test_resume_rejects_condition_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            manager = BuildComparisonJobManager(
                Path(directory), "http://127.0.0.1:8765/mcp"
            )
            resume = manager.output_root / "comparison_failed"
            resume.mkdir(parents=True)
            (resume / "comparison_request.json").write_text(
                json.dumps({
                    "seeds": [1001],
                    "build_profiles": ["standard"],
                    "player_level_requested": "intermediate",
                    "validation_variant": "fixed",
                    "valid_runs_per_build": 1,
                    "dynamic_balance": False,
                }),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "conditions do not match"):
                manager.start(
                    [1002],
                    ["standard"],
                    "intermediate",
                    "fixed",
                    1,
                    False,
                    str(resume),
                )
            with self.assertRaisesRegex(ValueError, "valid_runs_per_build"):
                manager.start(
                    [1001, 1002],
                    ["standard"],
                    "intermediate",
                    "fixed",
                    1,
                    False,
                )

    def test_completed_result_reads_saved_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            manager = BuildComparisonJobManager(
                Path(directory), "http://127.0.0.1:8765/mcp"
            )
            process = FakeProcess()
            with patch("build_comparison_jobs.subprocess.Popen", return_value=process):
                started = manager.start(
                    [1001],
                    ["standard"],
                    "intermediate",
                    "fixed",
                    1,
                    False,
                )
            output = Path(started["output_paths"]["output_directory"])
            (output / "comparison_status.json").write_text(
                json.dumps({
                    "state": "completed",
                    "completed_valid_runs": 1,
                    "excluded_runs": 0,
                }),
                encoding="utf-8",
            )
            (output / "build_summaries.json").write_text(
                json.dumps({
                    "builds_summary": {
                        "standard": {
                            "sample_count": 1,
                            "win_rate": 1.0,
                            "final_boss_reach_rate": 1.0,
                        }
                    },
                    "paired_seed_summary": {
                        "same_seed_outcome_changed_count": 0
                    },
                }),
                encoding="utf-8",
            )
            (output / "seed_build_comparison.csv").write_text(
                "run_seed,standard_win\n1001,1\n", encoding="utf-8-sig"
            )
            (output / "errors_and_exclusions.jsonl").write_text(
                "", encoding="utf-8"
            )
            process.return_code = 0

            result = manager.result()

            self.assertTrue(result["ok"])
            self.assertEqual(result["build_summaries"]["standard"]["sample_count"], 1)
            self.assertEqual(result["seed_build_comparison"][0]["run_seed"], "1001")
            self.assertEqual(result["errors_and_exclusions"], [])
            self.assertIn("build_summaries_json", result["saved_paths"])


if __name__ == "__main__":
    unittest.main()
