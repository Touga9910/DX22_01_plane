from __future__ import annotations

import json
import math
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

PROGRESSION_SOURCES = (
    "GameClearRewardFlow.cpp",
    "GamePocketLifecycle.cpp",
    "GameRestActions.cpp",
    "GameRewardOperations.cpp",
    "GameShopOperations.cpp",
    "GameRelicOperations.cpp",
    "GamePlayerBallLifecycle.cpp",
    "GameShotStart.cpp",
    "GameShotEnd.cpp",
    "GameReflectionSynergy.cpp",
    "GamePierceSynergy.cpp",
    "GameCollisionSynergy.cpp",
    "GameDamageAndDefeat.cpp",
    "GameCombatEventBridge.cpp",
    "GameDebugSnapshot.cpp",
)


def load(path: str) -> dict:
    with (ROOT / path).open("r", encoding="utf-8") as source:
        return json.load(source)


def load_game_sources() -> str:
    return "\n".join(
        (ROOT / name).read_text(encoding="utf-8")
        for name in (
            "Game.cpp",
            "GameAutoPlay.cpp",
            "GameBattleStart.cpp",
            "BalanceAutoPlayer.cpp",
            "BalanceAutoPlayerScenes.cpp",
            "BalanceAutoPlayerShots.cpp",
            "BalanceAutoPlayerRewards.cpp",
            "BalanceAutoPlayerBuildChoices.cpp",
            "BalanceAutoPlayerPending.cpp",
            "GameBalanceConfig.cpp",
            *PROGRESSION_SOURCES,
            "DynamicBalanceController.cpp",
            "BalanceValidationController.cpp",
        )
    )


class BalanceConfigurationTests(unittest.TestCase):
    def test_build_profile_is_a_logged_run_condition(self) -> None:
        profiles = load("tools/game_mcp/build_profiles.json")
        logger_source = (ROOT / "BalanceLogger.cpp").read_text(
            encoding="utf-8"
        )
        game_source = load_game_sources()

        self.assertEqual(profiles["schema_version"], 1)
        self.assertEqual(
            set(profiles["profiles"]),
            {"standard", "heavy", "pierce", "bounce", "anchor"},
        )
        self.assertIn('{ "schema_version", 4 }', logger_source)
        self.assertIn('"balance_version"', logger_source)
        self.assertIn('"balance_config_hash"', logger_source)
        self.assertIn('"measurement"', logger_source)
        self.assertIn('"build_profile_settings_hash"', logger_source)
        self.assertIn('{ "build_profile", buildProfile }', game_source)
        self.assertIn(
            '{ "build_profile_settings_hash", buildProfileSettingsHash }',
            game_source,
        )
        for name in ("GameMcpBattleCommands.cpp", "GameMcpEconomyCommands.cpp"):
            with self.subTest(source=name):
                self.assertIn(
                    '"mcp_build_decision"',
                    (ROOT / name).read_text(encoding="utf-8"),
                )

    def test_enemy_validation_conditions_and_shot_stacks_are_logged(self) -> None:
        game_source = load_game_sources()

        for field in (
            '"enemy_validation"',
            '"hp_multiplier"',
            '"enemy_count"',
            '"layout_pattern"',
            '"seed"',
            '"ball_category"',
        ):
            self.assertIn(field, game_source)
        fields_by_owner = {
            "BalanceLoggerShotResults.cpp": (
                "heavy_stacks_uncapped_gain", "heavy_stacks_generated",
                "heavy_stack_distance_per_stack", "heavy_stack_gain_limit_per_turn",
                "heavy_stacks_discarded_by_turn_limit",
                "heavy_stacks_discarded_by_storage_limit", "heavy_stacks_consumed",
                "heavy_stacks_before", "heavy_stacks_after",
                "pierce_stacks_generated", "pierce_stacks_uncapped_gain",
                "pierce_stacks_consumed", "pierce_stacks_before",
                "pierce_stacks_after", "pierce_stack_max", "pierce_stack_max_reached",
                "pierce_minion_fizzle_count",
            ),
            "BalanceLoggerShot.cpp": (
                "pierce_attack", "bounce_axis", "physics_chain",
                "reflection_stacks_uncapped_gain", "reflection_stacks_generated",
                "reflection_stacks_consumed", "reflection_stacks_before",
                "reflection_stacks_after",
            ),
            "BalanceLoggerTelemetryJson.cpp": (
                "physical_reflection_count", "effective_reflection_count",
                "timeline_event_index", "attack_timing", "attack_timeline",
                "post_reflection_effective_attack_succeeded", "pinball_activation_count",
                "pinball_fizzle_count", "pinball_applied_bonus_damage",
                "empowered_hit_activation_count", "effective_enemy_enemy_collision_count",
                "involved_enemy_count", "enemy_distances", "physics_processing_microseconds",
            ),
            "BalanceLoggerStage.cpp": ("turn_count",),
        }
        for name, fields in fields_by_owner.items():
            logger_source = (ROOT / name).read_text(encoding="utf-8")
            for field in fields:
                with self.subTest(source=name, field=field):
                    self.assertIn(f'"{field}"', logger_source)
        self.assertIn(
            '"latest_shot_physics_chain"',
            (ROOT / "GameMcpCombatState.cpp").read_text(encoding="utf-8"),
        )
        self.assertIn(
            '"latest_shot_pierce_attack"',
            (ROOT / "GameMcpCombatState.cpp").read_text(encoding="utf-8"),
        )
        self.assertIn(
            '"latest_shot_bounce_axis"',
            (ROOT / "GameMcpCombatState.cpp").read_text(encoding="utf-8"),
        )
        self.assertIn(
            '"latest_shot_combat_result"',
            (ROOT / "GameMcpCombatState.cpp").read_text(encoding="utf-8"),
        )
        self.assertIn(
            '"measurement_scope", "all_recorded_enemy_hp_damage"',
            (ROOT / "BalanceLoggerShot.cpp").read_text(encoding="utf-8"),
        )
        self.assertIn(
            '"measurement_scope", "pierce_attack_telemetry_sources_only"',
            (ROOT / "BalanceLoggerTelemetryJson.cpp").read_text(encoding="utf-8"),
        )
        self.assertIn(
            '"is_generic_combat_total", false',
            (ROOT / "BalanceLoggerTelemetryJson.cpp").read_text(encoding="utf-8"),
        )

    def test_mcp_enemy_state_supports_aim_error(self) -> None:
        bridge_source = (ROOT / "GameMcpCombatState.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn('{ "radius", enemy->GetRadius() }', bridge_source)

    def test_mcp_contract_exposes_percentage_rest_heal(self) -> None:
        state_source = (ROOT / "GameMcpRunState.cpp").read_text(
            encoding="utf-8"
        )
        read_view_source = (ROOT / "GameMcpReadView.cpp").read_text(
            encoding="utf-8"
        )
        server_source = (ROOT / "tools/game_mcp/server.py").read_text(
            encoding="utf-8"
        )

        self.assertIn('state["rest_heal"]', state_source)
        self.assertIn(
            "snapshot.restHealRatio = game.m_RunController->RestHealRatio()",
            read_view_source,
        )
        self.assertIn(
            '{ "heal_ratio", snapshot.restHealRatio }',
            state_source,
        )
        self.assertIn("最大HPの25%回復", server_source)
        self.assertNotIn("プレイヤーHPを全回復", server_source)

    def test_mcp_uses_the_same_offered_routes_as_human(self) -> None:
        stage_select_source = (ROOT / "StageSelectScene.cpp").read_text(
            encoding="utf-8"
        )
        command_source = (ROOT / "GameMcpRunCommands.cpp").read_text(
            encoding="utf-8"
        )
        state_source = (ROOT / "GameMcpAvailableActions.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn(
            'ChooseRoute(m_SelectedNode, "human");',
            stage_select_source,
        )
        self.assertIn(
            'stageSelect->ChooseRoute(routeIndex, "mcp")',
            command_source,
        )
        self.assertIn('state["route_options"]', state_source)
        self.assertNotIn('{ "offered_routes", nullptr }', state_source)

    def test_dynamic_balance_runtime_lifecycle_is_removed(self) -> None:
        game_source = load_game_sources()

        for retired_symbol in (
            "m_DynamicBalanceController.OnRunStarted()",
            "m_DynamicBalanceController.OnBattleStarted(",
            "m_DynamicBalanceController.OnShotStarted()",
            "m_DynamicBalanceController.OnShotFinished()",
            "DynamicBalance().OnHit()",
        ):
            self.assertNotIn(retired_symbol, game_source)

    def test_dynamic_balance_is_retired(self) -> None:
        dynamic = load("assets/data/dynamic_balance.json")
        game_source = load_game_sources()

        self.assertTrue(dynamic["retired"])
        for retired_key in (
            "enabled",
            "initial_level",
            "maximum_level",
            "minimum_level",
            "hp_step_per_level",
            "attack_step",
            "positive_attack_scaling_enabled",
            "strong_clear_hp_ratio",
            "weak_clear_hp_ratio",
            "strong_no_hit_rate",
            "weak_no_hit_rate",
            "target_shots_per_enemy",
            "weak_shot_multiplier",
        ):
            self.assertNotIn(retired_key, dynamic)
        self.assertIn("ApplyEnemyDifficultyScaling", game_source)
        self.assertNotIn("ApplyDynamicBalanceToEnemyData", game_source)
        self.assertNotIn(
            "enemyData.maxHp + effectiveLevel * m_HpStep",
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
                *PROGRESSION_SOURCES,
                *(path.name for path in sorted(ROOT.glob("GameMcp*.cpp"))),
                "GameSaveManager.cpp",
                "PlayerBallDataLoader.h",
                "PlayerBallDataLoader.cpp",
            )
        )

        self.assertEqual(player["maxHp"], 50)
        self.assertNotIn("maxHp", player["status"])
        self.assertEqual(player["currentHp"], 50)
        self.assertEqual(player["restHealRatio"], 0.25)
        self.assertEqual(player["heavyStackDistancePerStack"], 40.0)
        self.assertEqual(player["heavyStackGainLimitPerTurn"], 10)
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

    def test_mcp_balance_runs_do_not_inherit_selected_ascension(self) -> None:
        source = (ROOT / "GameBalanceConfig.cpp").read_text(encoding="utf-8")
        self.assertIn(
            'm_PersistenceRuntime->ActiveAscension() = (effectiveControllerType == "human" &&',
            source,
        )

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
        self.assertIn("m_EnduranceMode", game_source)
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
        world_source = (ROOT / "GameMcpWorldState.cpp").read_text(
            encoding="utf-8"
        )
        combat_source = (ROOT / "GameMcpCombatState.cpp").read_text(
            encoding="utf-8"
        )

        self.assertEqual(pocket["player_max_hp_damage_ratio"], 0.04)
        self.assertEqual(pocket["schema_version"], 2)
        self.assertTrue(pocket["enemy_instant_defeat"])
        self.assertEqual(
            math.ceil(
                50 * pocket["player_max_hp_damage_ratio"]
            ),
            2,
        )
        self.assertNotIn("enemy_finisher_hp_ratios", pocket)
        self.assertNotIn("enemy_return", pocket)
        self.assertIn('"enemy_pocket_defeat"', game_source)
        self.assertNotIn("QueuePocketedEnemy", game_source)
        self.assertNotIn("RestoreNextPocketedEnemy", game_source)
        self.assertIn('state["pocket_rules"]', world_source)
        self.assertIn('{ "instant_defeat",', world_source)
        self.assertIn('{ "pocketed", enemy->IsPocketed() }', combat_source)
        self.assertIn(
            '"pocket_finisher_eligible"',
            combat_source,
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
            if stage["id"].startswith("validation_") or stage.get(
                "prototypeOnly", False
            ):
                # Validation/prototype layouts intentionally do not
                # participate in the production encounter target table.
                self.assertNotIn(stage["id"], targets)
                continue
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
        # 比較実験は必要なときだけ明示的に有効化し、通常起動では
        # セーブや実績進行を妨げない。
        self.assertFalse(validation["enabled"])
        self.assertGreaterEqual(len(set(validation["random_seeds"])), 3)
        self.assertEqual(len(validation["random_seeds"]), 5)
        variant_ids = [variant["id"] for variant in validation["variants"]]
        self.assertIn("fixed", variant_ids)
        self.assertIn("normal_play_1112", variant_ids)
        production = next(
            variant for variant in validation["variants"]
            if variant["id"] == "normal_play_1112"
        )
        self.assertTrue(production["normal_play_balance"])
        self.assertEqual(validation["minimum_paired_seeds"], 0)
        self.assertEqual(
            set(validation["seed_suites"]),
            {"tuning", "holdout"},
        )
        self.assertFalse(
            set(validation["seed_suites"]["tuning"])
            & set(validation["seed_suites"]["holdout"])
        )

    def test_normal_play_stage_pools_cover_the_route_without_unlocking_early(self) -> None:
        normal_play = load("assets/data/normal_play_balance.json")
        stages = load("assets/data/stage_01.json")["stages"]
        normal_ids = {
            stage["id"] for stage in stages
            if stage.get("stageType", "normal") == "normal"
        }
        pools = normal_play["normal_stage_pools"]
        for progress in range(1, 16):
            matches = [
                pool for pool in pools
                if pool["min_progress"] <= progress <= pool["max_progress"]
            ]
            self.assertEqual(len(matches), 1)
            self.assertTrue(matches[0]["stage_ids"])
            self.assertTrue(set(matches[0]["stage_ids"]).issubset(normal_ids))

        opening = next(pool for pool in pools if pool["id"] == "opening")
        self.assertEqual(opening["stage_ids"], ["normal_001", "normal_002"])
        for progress in range(5, 10):
            pool = next(item for item in pools
                if item["min_progress"] <= progress <= item["max_progress"])
            self.assertEqual(set(pool["stage_ids"]), {
                "campaign_phase_mid_mixed", "campaign_phase_mid_pairs"})
        for progress in range(10, 16):
            pool = next(item for item in pools
                if item["min_progress"] <= progress <= item["max_progress"])
            self.assertEqual(set(pool["stage_ids"]), {
                "campaign_phase_late_mixed", "campaign_phase_late_pairs"})

        budgets = normal_play["enemy_action_budgets"]
        budget_by_progress = {
            progress: next(
                item["maximum_budget"] for item in budgets
                if item["min_progress"] <= progress <= item["max_progress"]
            )
            for progress in range(1, 16)
        }
        self.assertTrue(all(budget_by_progress[p] == 4 for p in range(1, 7)))
        self.assertTrue(all(budget_by_progress[p] == 5 for p in range(7, 16)))

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
