from __future__ import annotations

import json
import math
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def load(path: str) -> dict:
    with (ROOT / path).open("r", encoding="utf-8") as source:
        return json.load(source)


class BalanceConfigurationTests(unittest.TestCase):
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
        game_source = (ROOT / "Game.cpp").read_text(encoding="utf-8")

        self.assertIn("ResetDynamicBalanceRunState();", game_source)
        self.assertIn(
            "m_DynamicBalanceEnabled = m_DynamicBalanceConfiguredEnabled;",
            game_source,
        )
        self.assertIn(
            "m_DynamicBalanceLevel = std::clamp(",
            game_source,
        )

    def test_positive_dynamic_balance_scales_hp_only(self) -> None:
        dynamic = load("assets/data/dynamic_balance.json")
        game_source = (ROOT / "Game.cpp").read_text(encoding="utf-8")

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

        self.assertEqual(player["status"]["maxHp"], 50)
        self.assertEqual(player["currentHp"], 50)
        self.assertEqual(player["restHealRatio"], 0.25)
        self.assertEqual(player["restHealCooldownBattles"], 2)
        self.assertEqual(
            math.ceil(
                player["status"]["maxHp"] * player["restHealRatio"]
            ),
            13,
        )
        for ball in player["balls"]:
            self.assertEqual(ball["status"]["maxHp"], 50)

    def test_late_progression_and_validation_run_cap(self) -> None:
        dynamic = load("assets/data/dynamic_balance.json")
        validation = load("assets/data/balance_validation.json")
        game_source = (ROOT / "Game.cpp").read_text(encoding="utf-8")
        status_source = (ROOT / "BallStatusComponent.h").read_text(
            encoding="utf-8"
        )

        progression = dynamic["progression_scaling"]
        self.assertTrue(progression["enabled"])
        self.assertEqual(progression["attack_start_progress"], 20)
        self.assertEqual(progression["attack_interval"], 5)
        self.assertEqual(progression["maximum_attack_delta"], 8)
        self.assertEqual(validation["maximum_cleared_stages_per_run"], 30)
        self.assertIn('"validation_complete"', game_source)
        self.assertIn(
            "const int finalDamage = (std::max)(1, damage - GetDefense());",
            status_source,
        )

    def test_pocket_rules_and_mcp_contract(self) -> None:
        pocket = load("assets/data/pocket_rules.json")
        game_source = (ROOT / "Game.cpp").read_text(encoding="utf-8")
        bridge_source = (ROOT / "GameMcpBridge.cpp").read_text(
            encoding="utf-8"
        )

        self.assertEqual(pocket["player_max_hp_damage_ratio"], 0.05)
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

    def test_effective_hp_targets_at_dynamic_balance_plus_two(self) -> None:
        enemies = load("assets/data/enemy_data.json")
        stages = load("assets/data/stage_01.json")
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

        def effective_hp(stage_id: str) -> int:
            return sum(
                enemy_by_id[
                    spawn.get("enemyId", "enemy_normal")
                ]["status"]["maxHp"] + 2
                for spawn in stage_by_id[stage_id]["enemies"]
            )

        normal_average = (
            effective_hp("normal_003")
            + effective_hp("normal_004")
        ) / 2
        midboss_average = (
            effective_hp("midboss_001")
            + effective_hp("midboss_002")
        ) / 2
        self.assertGreaterEqual(normal_average, 24)
        self.assertLessEqual(normal_average, 28)
        self.assertGreaterEqual(midboss_average, 30)
        self.assertLessEqual(midboss_average, 36)

        for boss_id in ("boss_001", "boss_002"):
            boss_hp = effective_hp(boss_id)
            self.assertGreaterEqual(boss_hp, 38)
            self.assertLessEqual(boss_hp, 45)
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


if __name__ == "__main__":
    unittest.main()
