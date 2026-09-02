import unittest
from pathlib import Path

from build_profiles import (
    BuildProfileController,
    compose_control_profile,
    load_build_profiles,
    profile_settings_hash,
)


class BuildProfileTests(unittest.TestCase):
    def setUp(self) -> None:
        path = Path(__file__).resolve().parent / "build_profiles.json"
        self.profiles, self.default_profile = load_build_profiles(path)

    def test_all_required_profiles_load(self) -> None:
        self.assertEqual(self.default_profile, "standard")
        self.assertEqual(
            set(self.profiles),
            {"standard", "heavy", "pierce", "bounce", "anchor"},
        )

    def test_hash_is_stable_and_profile_specific(self) -> None:
        first = profile_settings_hash(self.profiles["standard"])
        second = profile_settings_hash(self.profiles["standard"])
        self.assertEqual(first, second)
        self.assertNotEqual(
            first,
            profile_settings_hash(self.profiles["heavy"]),
        )
        self.assertEqual(len(first), 64)

    def test_controller_switches_profile(self) -> None:
        controller = BuildProfileController(
            self.profiles,
            self.default_profile,
        )
        snapshot = controller.set_profile("pierce")
        self.assertEqual(snapshot["id"], "pierce")
        self.assertEqual(controller.profile_id, "pierce")

    def test_composition_keeps_skill_and_overrides_build_policy(self) -> None:
        player = {
            "player_level": "intermediate",
            "pocket_tactics": {"minimum_alignment": 0.55},
            "relic_policy": {"low_hp_ratio": 0.5},
            "instructions": ["skill"],
        }
        build = BuildProfileController(
            self.profiles,
            "anchor",
        ).snapshot()
        composed = compose_control_profile(player, build)
        self.assertEqual(composed["player_level"], "intermediate")
        self.assertEqual(composed["build_profile"]["id"], "anchor")
        self.assertEqual(
            composed["pocket_tactics"]["control_attack_weight"],
            5.5,
        )
        self.assertIn("skill", composed["instructions"])


if __name__ == "__main__":
    unittest.main()
