from __future__ import annotations

import json
import math
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def load(path: str) -> dict:
    with (ROOT / path).open("r", encoding="utf-8") as source:
        return json.load(source)


def load_game_sources() -> str:
    return "\n".join(
        (ROOT / name).read_text(encoding="utf-8")
        for name in (
            "Game.cpp",
            "GameAutoPlay.cpp",
            "GameBalanceConfig.cpp",
            "GameProgression.cpp",
        )
    )


class BalanceConfigurationTests(unittest.TestCase):
    def test_build_profile_is_a_logged_run_condition(self) -> None:
        profiles = load("tools/game_mcp/build_profiles.json")
        logger_source = (ROOT / "BalanceLogger.cpp").read_text(
            encoding="utf-8"
        )
        game_source = load_game_sources()
        bridge_source = (ROOT / "GameMcpBridge.cpp").read_text(
            encoding="utf-8"
        )

        self.assertEqual(profiles["schema_version"], 1)
        self.assertEqual(
            set(profiles["profiles"]),
            {"standard", "heavy", "pierce", "bounce", "anchor"},
        )
        self.assertIn('{ "schema_version", 3 }', logger_source)
        self.assertIn(
            '"tools/game_mcp/build_profiles.json"',
            logger_source,
        )
        self.assertIn('{ "build_profile", buildProfile }', game_source)
        self.assertIn(
            '{ "build_profile_settings_hash", buildProfileSettingsHash }',
            game_source,
        )
        self.assertIn('"mcp_build_decision"', bridge_source)

    def test_mcp_enemy_state_supports_aim_error(self) -> None:
        bridge_source = (ROOT / "GameMcpBridge.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn('{ "radius", enemy->GetRadius() }', bridge_source)

    def test_mcp_contract_exposes_percentage_rest_heal(self) -> None:
        bridge_source = (ROOT / "GameMcpBridge.cpp").read_text(
            encoding="utf-8"
        )
        server_source = (ROOT / "tools/game_mcp/server.py").read_text(
            encoding="utf-8"
        )

        self.assertIn('state["rest_heal"]', bridge_source)
        self.assertIn('{ "heal_ratio", game.m_RestHealRatio }', bridge_source)
        self.assertIn("最大HPの25%回復", server_source)
        self.assertNotIn("プレイヤーHPを全回復", server_source)

    def test_mcp_uses_the_same_offered_routes_as_human(self) -> None:
        stage_select_source = (ROOT / "StageSelectScene.cpp").read_text(
            encoding="utf-8"
        )
        bridge_source = (ROOT / "GameMcpBridge.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn(
            'ChooseRoute(m_SelectedNode, "human");',
            stage_select_source,
        )
        self.assertIn(
            'stageSelect->ChooseRoute(routeIndex, "mcp")',
            bridge_source,
        )
        self.assertIn('state["route_options"]', bridge_source)
        self.assertNotIn('{ "offered_routes", nullptr }', bridge_source)

    def test_new_run_resets_dynamic_balance_state(self) -> None:
        game_source = load_game_sources()

        self.assertIn("ResetDynamicBalanceRunState();", game_source)
        self.assertIn(
            "m_DynamicBalanceEnabled = m_DynamicBalanceConfiguredEnabled;",
            game_source,
        )
        self.assertIn(
            "m_DynamicBalanceLevel = std::clamp(",
            game_source,
        )

    def test_dynamic_balance_is_assist_only(self) -> None:
        dynamic = load("assets/data/dynamic_balance.json")
        game_source = load_game_sources()

        self.assertEqual(dynamic["initial_level"], 0)
        self.assertEqual(dynamic["maximum_level"], 0)
        self.assertLess(dynamic["minimum_level"], 0)
        self.assertFalse(dynamic["positive_attack_scaling_enabled"])
        self.assertIn(
            "level > 0 && !m_DynamicBalancePositiveAttackScalingEnabled",
            game_source,
        )
        self.assertIn(
            "CalculateDynamicBalanceAttackModifier(effectiveLevel)",
            game_source,
        )

    def test_player_hp_and_rest_heal_ratio(self) -> None:
        player = load("assets/data/player_status.json")
        cooldown_sources = "\n".join(
            (ROOT / name).read_text(encoding="utf-8")
            for name in (
                "Game.h",
                "Game.cpp",
                "GameBalanceConfig.cpp",
                "GameProgression.cpp",
                "GameMcpBridge.cpp",
                "GameSaveManager.cpp",
                "PlayerBallDataLoader.h",
                "PlayerBallDataLoader.cpp",
            )
        )

        self.assertEqual(player["maxHp"], 50)
        self.assertNotIn("maxHp", player["status"])
        self.assertEqual(player["currentHp"], 50)
        self.assertEqual(player["restHealRatio"], 0.25)
        self.assertNotIn("restHealCooldownBattles", player)
        self.assertNotIn("resthealcooldown", cooldown_sources.lower())
        self.assertEqual(
            math.ceil(
                player["maxHp"] * player["restHealRatio"]
            ),
            13,
        )
        for ball in player["balls"]:
            self.assertNotIn("maxHp", ball["status"])

    def test_late_progression_and_validation_run_cap(self) -> None:
        dynamic = load("assets/data/dynamic_balance.json")
        validation = load("assets/data/balance_validation.json")
        game_source = load_game_sources()
        status_source = (ROOT / "BallStatusComponent.h").read_text(
            encoding="utf-8"
        )

        progression = dynamic["progression_scaling"]
        self.assertTrue(progression["enabled"])
        self.assertEqual(progression["hp_start_progress"], 10)
        self.assertEqual(progression["hp_interval"], 5)
        self.assertEqual(progression["hp_step"], 1)
        self.assertEqual(progression["maximum_hp_delta"], 4)
        self.assertEqual(progression["attack_start_progress"], 20)
        self.assertEqual(progression["attack_interval"], 5)
        self.assertEqual(progression["maximum_attack_delta"], 8)
        self.assertEqual(validation["maximum_cleared_stages_per_run"], 30)
        self.assertFalse(validation["endurance_mode"])
        self.assertIn("m_BalanceValidationEnduranceMode", game_source)
        self.assertIn('"validation_complete"', game_source)
        self.assertIn(
            "return (std::max)(1, damage - GetDefense());",
            status_source,
        )
        self.assertIn(
            "const int finalDamage = CalculateDamageTaken(damage);",
            status_source,
        )

    def test_pocket_rules_and_mcp_contract(self) -> None:
        pocket = load("assets/data/pocket_rules.json")
        game_source = load_game_sources()
        bridge_source = (ROOT / "GameMcpBridge.cpp").read_text(
            encoding="utf-8"
        )

        self.assertEqual(pocket["player_max_hp_damage_ratio"], 0.04)
        self.assertEqual(
            math.ceil(
                50 * pocket["player_max_hp_damage_ratio"]
            ),
            2,
        )
        self.assertEqual(
            pocket["enemy_finisher_hp_ratios"],
            {"normal": 0.30, "midboss": 0.20, "boss": 0.10},
        )
        self.assertEqual(pocket["enemy_return"]["per_turn"], 1)
        self.assertIn("RestoreNextPocketedEnemy();", game_source)
        self.assertIn('state["pocket_rules"]', bridge_source)
        self.assertIn('{ "pocketed", enemy->IsPocketed() }', bridge_source)
        self.assertIn(
            '"pocket_finisher_eligible"',
            bridge_source,
        )

    def test_stage_enemy_references_and_threat_budgets(self) -> None:
        enemies = load("assets/data/enemy_data.json")
        stages = load("assets/data/stage_01.json")
        encounter = load("assets/data/encounter_balance.json")
        enemy_ids = {enemy["id"] for enemy in enemies["enemies"]}
        costs = encounter["enemy_costs"]
        targets = encounter["stage_target_budgets"]

        for stage in stages["stages"]:
            ids = [
                spawn.get("enemyId", "enemy_normal")
                for spawn in stage["enemies"]
            ]
            self.assertTrue(set(ids).issubset(enemy_ids))
            self.assertTrue(set(ids).issubset(costs))
            layout = (
                "dense_auto_layout"
                if len(ids) >= 4
                else "stage_data"
            )
            actual = sum(float(costs[enemy_id]) for enemy_id in ids)
            actual *= float(encounter["layout_multipliers"][layout])
            self.assertAlmostEqual(actual, float(targets[stage["id"]]))

    def test_effective_hp_targets_with_progression_scaling(self) -> None:
        enemies = load("assets/data/enemy_data.json")
        stages = load("assets/data/stage_01.json")
        dynamic = load("assets/data/dynamic_balance.json")
        enemy_by_id = {
            enemy["id"]: enemy
            for enemy in enemies["enemies"]
        }
        stage_by_id = {
            stage["id"]: stage
            for stage in stages["stages"]
        }

        expected_attacks = {
            "enemy_normal": 1,
            "enemy_strong": 2,
            "enemy_tank": 1,
            "enemy_striker": 3,
            "enemy_boss_core": 1,
        }
        for enemy_id, attack in expected_attacks.items():
            self.assertEqual(
                enemy_by_id[enemy_id]["status"]["attack"],
                attack,
            )

        progression = dynamic["progression_scaling"]

        def hp_delta(progress: int) -> int:
            if progress < progression["hp_start_progress"]:
                return 0
            tier = 1 + (
                progress - progression["hp_start_progress"]
            ) // progression["hp_interval"]
            return min(
                progression["maximum_hp_delta"],
                tier * progression["hp_step"],
            )

        def effective_hp(stage_id: str, progress: int) -> int:
            return sum(
                enemy_by_id[
                    spawn.get("enemyId", "enemy_normal")
                ]["status"]["maxHp"] + (0 if spawn.get("enemyId") == "enemy_boss_core" else hp_delta(progress))
                for spawn in stage_by_id[stage_id]["enemies"]
            )

        normal_average = (
            effective_hp("normal_003", 15)
            + effective_hp("normal_004", 15)
        ) / 2
        midboss_average = (
            effective_hp("midboss_001", 15)
            + effective_hp("midboss_002", 15)
        ) / 2
        self.assertGreaterEqual(normal_average, 24)
        self.assertLessEqual(normal_average, 28)
        self.assertGreaterEqual(midboss_average, 24)
        self.assertLessEqual(midboss_average, 30)

        for boss_id in ("boss_001", "boss_002"):
            boss_hp = effective_hp(boss_id, 10)
            self.assertEqual(boss_hp, 60)
            self.assertEqual(len(stage_by_id[boss_id]["enemies"]), 1)
            boss_enemy_ids = {
                spawn.get("enemyId", "enemy_normal")
                for spawn in stage_by_id[boss_id]["enemies"]
            }
            self.assertIn("enemy_boss_core", boss_enemy_ids)

    def test_validation_baseline_profile_exists(self) -> None:
        profiles = load("assets/data/difficulty_profiles.json")
        validation = load("assets/data/balance_validation.json")
        self.assertIn(profiles["selected_profile"], profiles["profiles"])
        self.assertIn(validation["baseline_profile"], profiles["profiles"])
        self.assertTrue(validation["enabled"])
        self.assertGreaterEqual(len(set(validation["random_seeds"])), 3)
        self.assertEqual(len(validation["random_seeds"]), 5)
        self.assertEqual(
            [variant["id"] for variant in validation["variants"]],
            ["dda_off", "dda_on"],
        )
        self.assertEqual(validation["minimum_paired_seeds"], 5)
        self.assertEqual(
            set(validation["seed_suites"]),
            {"tuning", "holdout"},
        )
        self.assertFalse(
            set(validation["seed_suites"]["tuning"])
            & set(validation["seed_suites"]["holdout"])
        )

    def test_stage_and_run_balance_targets_are_separated(self) -> None:
        targets = load("assets/data/balance_targets.json")

        self.assertEqual(targets["schema_version"], 3)
        self.assertTrue(targets["statistical_decision"]["enabled"])
        self.assertIn("difficulty_spikes", targets["diagnostics"])
        stage_targets = targets["stage_type_targets"]
        self.assertEqual(
            set(stage_targets),
            {"normal", "midBoss", "boss"},
        )
        self.assertLess(
            stage_targets["normal"]["metrics"]["median_shots"]["max"],
            stage_targets["boss"]["metrics"]["median_shots"]["min"],
        )

        run_targets = targets["run_targets"]
        self.assertEqual(run_targets["default_profile"], "intermediate")
        self.assertEqual(
            set(run_targets["profile_overrides"]),
            {"beginner", "advanced"},
        )
        self.assertAlmostEqual(
            sum(
                float(metric["weight"])
                for metric in run_targets["metrics"].values()
            ),
            1.0,
        )
        for metric in run_targets["metrics"].values():
            self.assertLessEqual(metric["min"], metric["max"])
            self.assertIn(
                metric["difficulty_direction"],
                {"easier", "harder", "neutral"},
            )


if __name__ == "__main__":
    unittest.main()
