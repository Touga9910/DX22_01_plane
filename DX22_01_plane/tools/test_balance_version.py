from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from balance_version import (
    calculate_config_hash,
    next_version,
    validate_registry,
)


class BalanceVersionTests(unittest.TestCase):
    def test_hash_is_path_order_independent_and_detects_content_change(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            (root / "a.json").write_text("{}\n", encoding="utf-8")
            (root / "b.json").write_text(
                json.dumps({"value": 1}),
                encoding="utf-8",
            )
            first, missing = calculate_config_hash(
                root,
                ["b.json", "a.json"],
            )
            reordered, _ = calculate_config_hash(
                root,
                ["a.json", "b.json"],
            )
            self.assertEqual(first, reordered)
            self.assertEqual(missing, [])

            (root / "b.json").write_text(
                json.dumps({"value": 2}),
                encoding="utf-8",
            )
            changed, _ = calculate_config_hash(root, ["a.json", "b.json"])
            self.assertNotEqual(first, changed)

    def test_validation_rejects_changed_settings_without_bump(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            (root / "rules.json").write_text("{}\n", encoding="utf-8")
            registered, _ = calculate_config_hash(root, ["rules.json"])
            registry = {
                "current_version": "B0001",
                "current_config_hash": registered,
                "managed_files": ["rules.json"],
            }
            _, _, errors = validate_registry(root, registry)
            self.assertEqual(errors, [])

            (root / "rules.json").write_text(
                '{"damage": 2}\n',
                encoding="utf-8",
            )
            _, _, errors = validate_registry(root, registry)
            self.assertTrue(any("without a version bump" in e for e in errors))

    def test_version_increment_is_explicit_and_monotonic(self) -> None:
        self.assertEqual(next_version("B0001"), "B0002")
        self.assertEqual(next_version("B0099"), "B0100")
        with self.assertRaises(ValueError):
            next_version("2026.11")


if __name__ == "__main__":
    unittest.main()
