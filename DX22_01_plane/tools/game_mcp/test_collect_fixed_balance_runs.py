from __future__ import annotations

import asyncio
import copy
import json
import tempfile
import unittest
import httpx
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import ANY, AsyncMock, patch

from build_profiles import load_build_profiles
from collect_fixed_balance_runs import (
    aggregate_build_records,
    call,
    catalog_candidate,
    choose_shot_type,
    collect_comparison_suite,
    extract_run_record,
    midboss_relic_candidate,
    offered_ball_score,
    is_terminal_summary,
    paired_seed_metrics,
    play_current_run,
    response_player_level,
    run_mcp_operation_with_retries,
    set_and_confirm_player_level,
    upgrade_candidate,
    validate_comparison_record,
    validate_resume_inputs,
    write_comparison_outputs,
    McpRetryExhausted,
    _write_comparison_status,
)


class BuildCollectorDecisionTests(unittest.TestCase):
    def test_collection_continues_after_a_normal_loss(self) -> None:
        self.assertTrue(is_terminal_summary({"completed": False, "reason": "run_ended_before_boss_defeat"}))
        self.assertTrue(is_terminal_summary({"completed": True, "reason": "final_boss_defeated"}))
        self.assertFalse(is_terminal_summary({"completed": False, "reason": "maximum_steps_reached"}))

    def setUp(self) -> None:
        path = Path(__file__).resolve().parent / "build_profiles.json"
        self.profiles, _ = load_build_profiles(path)

    def test_each_build_selects_its_priority_catalog_ball(self) -> None:
        catalog = [
            {"index": index, "definition_id": definition_id}
            for index, definition_id in enumerate((
                "player_standard",
                "player_heavy",
                "player_pierce",
                "player_bounce",
                "player_anchor",
            ))
        ]
        state = {"catalog_balls": catalog}
        expected = {
            "standard": "player_standard",
            "heavy": "player_heavy",
            "pierce": "player_pierce",
            "bounce": "player_bounce",
            "anchor": "player_anchor",
        }
        for profile_id, definition_id in expected.items():
            with self.subTest(profile_id=profile_id):
                selected = catalog_candidate(
                    state,
                    self.profiles[profile_id],
                )
                self.assertIsNotNone(selected)
                self.assertEqual(selected["definition_id"], definition_id)

    def test_upgrade_priority_precedes_upgrade_level(self) -> None:
        state = {
            "deck_balls": [
                {
                    "instance_id": "standard-low",
                    "definition_id": "player_standard",
                    "upgrade_level": 0,
                    "can_upgrade": True,
                    "status": {"attack": 1},
                },
                {
                    "instance_id": "heavy-high",
                    "definition_id": "player_heavy",
                    "upgrade_level": 1,
                    "can_upgrade": True,
                    "status": {"attack": 2},
                },
            ]
        }
        selected = upgrade_candidate(state, self.profiles["heavy"])
        self.assertEqual(selected["instance_id"], "heavy-high")

    def test_pierce_score_rewards_multi_enemy_pierce(self) -> None:
        offer = {
            "definition_id": "player_pierce",
            "status": {
                "attack": 1,
                "defense": 0,
                "mass": 1,
                "restitution": 0,
                "pierce": True,
            },
        }
        policy = self.profiles["pierce"]
        self.assertGreater(
            offered_ball_score(offer, policy, 2),
            offered_ball_score(offer, policy, 1),
        )

    def test_bounce_uses_bank_only_when_skill_allows_it(self) -> None:
        state = {"mcp_control": {"allow_bank_shots": True}}
        self.assertEqual(
            choose_shot_type(state, self.profiles["bounce"], "damage", 0),
            "bank",
        )
        state["mcp_control"]["allow_bank_shots"] = False
        self.assertEqual(
            choose_shot_type(state, self.profiles["bounce"], "damage", 0),
            "direct",
        )

    def midboss_state(self) -> dict:
        return {
            "scene": "battle",
            "game_state": "clear_reward",
            "available_actions": ["choose_relic"],
            "player": {"current_hp": 13, "max_hp": 50, "money": 0},
            "relics": [
                {"index": 0, "name": "Power Core", "price": 0},
                {"index": 1, "name": "Guard Core", "price": 0,
                 "owned": True, "midboss_offered": True},
                {"index": 4, "name": "緊急修理キット", "price": 20,
                 "midboss_offered": True},
                {"index": 5, "name": "精密照準器", "price": 18,
                 "midboss_offered": True},
                {"index": 7, "name": "貫通過給機", "price": 22,
                 "midboss_offered": True},
            ],
        }

    def test_midboss_reward_is_free_and_prefers_survival_at_low_hp(self) -> None:
        state = self.midboss_state()
        original = copy.deepcopy(state)
        choice = midboss_relic_candidate(state, self.profiles, "standard")
        self.assertEqual(choice["index"], 4)
        self.assertEqual(choice["breakdown"]["price_pressure"], 0)
        self.assertEqual(state, original)

    def test_midboss_reward_excludes_unoffered_and_owned_relics(self) -> None:
        state = self.midboss_state()
        state["relics"] = state["relics"][:2]
        self.assertIsNone(
            midboss_relic_candidate(state, self.profiles, "standard"),
        )

    def test_collector_selects_midboss_relic_before_normal_reward(self) -> None:
        state = self.midboss_state()
        # Even if both are exposed, the midboss reward must be resolved first.
        state["available_actions"].append("choose_reward")
        normal_reward = {
            "scene": "battle", "available_actions": ["choose_reward"],
            "build_decision": {"clear_reward": {"recommended": {
                "reward": "extra_money", "score": 1,
            }}},
        }
        with patch("collect_fixed_balance_runs.get_state", new_callable=AsyncMock) as read:
            with patch("collect_fixed_balance_runs.call", new_callable=AsyncMock) as send:
                read.side_effect = [
                    state,
                    normal_reward,
                    {"scene": "battle", "available_actions": [], "deck_balls": []},
                    {"scene": "result", "run_progress": {"final_boss_defeated": True}},
                ]
                send.return_value = {"ok": True}
                result = asyncio.run(play_current_run(
                    object(), "intermediate", "standard",
                    self.profiles["standard"], self.profiles, "test", 3, 1,
                ))
        self.assertTrue(result["completed"])
        self.assertEqual(result["actions"], 2)
        self.assertEqual(result["transient_errors"], 0)
        self.assertEqual([item.args[1] for item in send.await_args_list],
                         ["choose_relic", "choose_reward"])
        self.assertEqual(send.await_args_list[0].args[2]["relic_index"], 4)

    def test_collector_uses_boss_plan_without_legacy_enemy_targeting(self):
        state = {
            "scene": "battle", "boss_state": {"hp": 60},
            "available_actions": ["select_ball", "fire_shot", "fire_boss_shot"],
            "boss_shot_choices": {"state_key": "current", "recommended": {"candidate_id": "0:4"}},
        }
        with patch("collect_fixed_balance_runs.get_state", new_callable=AsyncMock) as read:
            with patch("collect_fixed_balance_runs.call", new_callable=AsyncMock) as send:
                read.side_effect = [state, {"scene": "result"}]
                send.return_value = {"ok": True}
                result = asyncio.run(play_current_run(
                    object(), "intermediate", "standard", self.profiles["standard"],
                    self.profiles, "test", 2, 1))
        self.assertEqual(result["shots"], 1)
        self.assertEqual([item.args[1] for item in send.await_args_list], ["fire_boss_shot"])
        self.assertEqual(send.await_args_list[0].args[2], {"candidate_id": "0:4", "state_key": "current"})

    def test_result_without_boss_defeat_is_not_a_completed_run(self):
        with patch("collect_fixed_balance_runs.get_state", new_callable=AsyncMock) as read:
            read.return_value = {"scene": "result", "run_progress": {"final_boss_defeated": False}}
            result = asyncio.run(play_current_run(
                object(), "intermediate", "standard", self.profiles["standard"],
                self.profiles, "test", 1, 1))
        self.assertFalse(result["completed"])
        self.assertEqual(result["reason"], "run_ended_before_boss_defeat")

    def test_collector_refreshes_target_after_selecting_ball(self):
        before = {
            "scene": "battle", "game_state": "aiming_direction",
            "available_actions": ["select_ball", "fire_shot"],
            "enemies": [{"target_id": "enemy:old", "attack": 1}],
            "offered_balls": [{"index": 1, "definition_id": "player_pierce"}],
            "build_decision": {"offered_ball_choices": {"recommended": {"index": 1}}},
            "shot_tactics": {"recommended_target_id": "enemy:old"},
            "recommended_action": {"action": "select_ball", "ball_index": 1},
        }
        after = {
            "scene": "battle", "game_state": "aiming_direction",
            "available_actions": ["select_ball", "fire_shot"],
            "offered_balls": [{"index": 1, "definition_id": "player_pierce", "selected": True}],
            "shot_tactics": {"recommended_target_id": "enemy:new"},
            "recommended_action": {"action": "fire_shot", "target_id": "enemy:new",
                                   "shot_goal": "damage", "shot_type": "direct"},
        }
        with patch("collect_fixed_balance_runs.get_state", new_callable=AsyncMock) as read:
            with patch("collect_fixed_balance_runs.call", new_callable=AsyncMock) as send:
                read.side_effect = [before, after, after, {"scene": "result", "run_progress": {"final_boss_defeated": True}}]
                send.return_value = {"ok": True}
                result = asyncio.run(play_current_run(
                    object(), "intermediate", "standard", self.profiles["standard"],
                    self.profiles, "test", 3, 1,
                ))
        self.assertTrue(result["completed"])
        self.assertEqual(send.await_args_list[0].args[1], "select_ball")
        fired = send.await_args_list[1].args[2]
        self.assertEqual(fired["target_id"], "enemy:new")
        self.assertEqual(fired["power_mode"], "auto")
        self.assertEqual(fired["shot_type"], "direct")


class ComparisonCollectionTests(unittest.TestCase):
    def test_reads_current_player_level_response_shape(self) -> None:
        self.assertEqual(
            response_player_level({
                "mcp_control": {"player_level": "intermediate"}
            }),
            "intermediate",
        )
        self.assertEqual(
            response_player_level({
                "result": {
                    "mcp_control": {"player_level": "intermediate"}
                }
            }),
            "intermediate",
        )

    def test_sets_then_confirms_player_level_from_fresh_state(self) -> None:
        set_response = {
            "ok": True,
            "mcp_control": {"player_level": "intermediate"},
        }
        state_response = {
            "mcp_control": {"player_level": "intermediate"}
        }
        with patch(
            "collect_fixed_balance_runs.call",
            new_callable=AsyncMock,
            return_value=set_response,
        ) as set_level:
            with patch(
            "collect_fixed_balance_runs.get_state",
            new_callable=AsyncMock,
            side_effect=[
                {"mcp_control": {"player_level": "beginner"}},
                state_response,
            ],
            ) as get_fresh_state:
                result, state = asyncio.run(
                    set_and_confirm_player_level(object(), "intermediate")
                )
        self.assertIs(result, set_response)
        self.assertIs(state, state_response)
        set_level.assert_awaited_once_with(
            ANY,
            "set_player_level",
            {"player_level": "intermediate"},
        )
        self.assertEqual(get_fresh_state.await_count, 2)

    def test_level_setup_does_not_resend_when_state_already_matches(self) -> None:
        state = {"mcp_control": {"player_level": "intermediate"}}
        with patch(
            "collect_fixed_balance_runs.call", new_callable=AsyncMock
        ) as set_level:
            with patch(
                "collect_fixed_balance_runs.get_state",
                new_callable=AsyncMock,
                return_value=state,
            ):
                result, confirmed = asyncio.run(
                    set_and_confirm_player_level(object(), "intermediate")
                )
        self.assertTrue(result["already_configured"])
        self.assertIs(confirmed, state)
        set_level.assert_not_awaited()

    def test_player_level_confirmation_still_rejects_mismatch(self) -> None:
        with patch(
            "collect_fixed_balance_runs.call",
            new_callable=AsyncMock,
            return_value={
                "ok": True,
                "mcp_control": {"player_level": "intermediate"},
            },
        ):
            with patch(
                "collect_fixed_balance_runs.get_state",
                new_callable=AsyncMock,
                side_effect=[
                    {"mcp_control": {"player_level": "beginner"}},
                    {"mcp_control": {"player_level": "beginner"}},
                ],
            ):
                with self.assertRaisesRegex(
                    RuntimeError, "confirmed=beginner"
                ):
                    asyncio.run(
                        set_and_confirm_player_level(
                            object(), "intermediate"
                        )
                    )

    def test_taskgroup_connect_error_reconnects_and_continues(self) -> None:
        retries = []
        transport_error = ExceptionGroup(
            "unhandled errors in a TaskGroup",
            [httpx.ConnectError("All connection attempts failed")],
        )
        with patch(
            "collect_fixed_balance_runs._run_mcp_session_operation",
            new_callable=AsyncMock,
            side_effect=[transport_error, {"scene": "battle"}],
        ) as execute:
            with patch(
                "collect_fixed_balance_runs.asyncio.sleep",
                new_callable=AsyncMock,
            ):
                result = asyncio.run(
                    run_mcp_operation_with_retries(
                        "http://127.0.0.1:8765/mcp",
                        "play_current_run",
                        AsyncMock(),
                        max_attempts=3,
                        base_delay=0,
                        on_retry=retries.append,
                    )
                )
        self.assertEqual(result["scene"], "battle")
        self.assertEqual(execute.await_count, 2)
        self.assertEqual(retries[0]["exception_type"], "ConnectError")
        self.assertEqual(retries[0]["retry_count"], 1)

    def test_connection_retry_is_bounded(self) -> None:
        with patch(
            "collect_fixed_balance_runs._run_mcp_session_operation",
            new_callable=AsyncMock,
            side_effect=ConnectionError("offline"),
        ) as execute:
            with patch(
                "collect_fixed_balance_runs.asyncio.sleep",
                new_callable=AsyncMock,
            ):
                with self.assertRaises(McpRetryExhausted) as raised:
                    asyncio.run(
                        run_mcp_operation_with_retries(
                            "http://127.0.0.1:8765/mcp",
                            "get_game_state",
                            AsyncMock(),
                            max_attempts=3,
                            base_delay=0,
                        )
                    )
        self.assertEqual(execute.await_count, 3)
        self.assertEqual(raised.exception.attempts, 3)

    @staticmethod
    def heavy_run() -> tuple[dict, dict]:
        run = {
            "schema_version": 4,
            "run_id": "run_heavy_1001",
            "build": {"compiled_date": "Sep 23 2026", "compiled_time": "12:00:00"},
            "configuration": {"files": [
                {"path": "assets/data/stage_01.json", "exists": True,
                 "fnv1a64": "abc", "size_bytes": 10},
            ]},
            "controller": {
                "type": "mcp", "profile": "intermediate",
                "build_profile": "heavy", "build_profile_settings_hash": "hash-heavy",
            },
            "run_context": {
                "controller_profile": "intermediate", "build_profile": "heavy",
                "build_profile_settings_hash": "hash-heavy", "initial_progress": 1,
                "dynamic_balance_enabled_at_start": False,
                "randomness": {"run_seed": 1001},
                "validation": {
                    "variant_id": "fixed", "dynamic_balance_forced_off": True
                },
            },
            "initial_player": {"deck": [{"id": "player_heavy"}]},
            "events": [
                {"event_type": "clear_reward_offered", "stage_index": 3,
                 "details": {"new_ball_candidates": [
                     {"ball_id": "player_chain_impact"}, {"ball_id": "player_standard"}
                 ], "upgrade_candidates": []}},
                {"event_type": "clear_reward_choice", "stage_index": 3,
                 "details": {"reward": "new_ball", "selected_index": 0}},
                {"event_type": "heavy_collision_recorded", "details": {"count": 1}},
                {"event_type": "chain_impact_triggered", "details": {"hits": 2}},
                {"event_type": "rest_heal", "details": {"heal_amount": 5}},
                {"event_type": "relic_purchased", "details": {"relic_name": "Impact Accelerator"}},
                {"event_type": "player_pocket_returned", "details": {}},
            ],
            "stages": [{
                "stage_index": 1, "stage_type": "normal",
                "stage_context": {
                    "assist_mode": {"enabled": False},
                    "dynamic_balance": {"enabled": False},
                    "deck": [{"id": "player_heavy"}, {"id": "player_chain_impact"}],
                },
                "stage_start": {"player_hp": 50},
                "stage_result": {"player_hp": 45},
                "shots": [{
                    "ball_id": "player_heavy", "total_enemy_damage": 6,
                    "total_damage_collision_count": 3, "enemy_enemy_hit_count": 2,
                    "player_enemy_damage": 4,
                }],
            }],
            "run_result": {
                "result": "completed", "reached_stage_index": 17, "player_hp": 45,
            },
        }
        summary = {
            "completed": True, "reason": "final_boss_defeated", "run_seed": 1001,
            "build_profile": "heavy", "progress": 17, "hp": 45,
            "run_progress": {"final_boss_reached": True, "final_boss_defeated": True},
        }
        return run, summary

    def test_extracts_requested_comparison_metrics(self) -> None:
        run, summary = self.heavy_run()
        record = extract_run_record(run, summary, Path("run.json"))
        self.assertTrue(record["normal_end"])
        self.assertEqual(record["run_seed"], 1001)
        self.assertEqual(record["ball_usage_by_definition"], {"player_heavy": 1})
        self.assertEqual(record["acquired_balls"], {"player_chain_impact": 1})
        self.assertEqual(record["battle_hp"][0]["hp_consumed"], 5)
        self.assertEqual(record["damage_total"], 6)
        self.assertEqual(record["collision_count"], 3)
        self.assertEqual(record["chain_count"], 2)
        self.assertEqual(record["build_completion_progress"], 3)
        self.assertEqual(record["build_reward_offer_count"], 1)
        self.assertEqual(record["build_reward_offer_not_taken_count"], 0)
        self.assertEqual(record["build_mechanic_activation_counts"]["chain_impact_triggered"], 1)
        self.assertEqual(
            validate_comparison_record(
                record, 1001, "heavy", "hash-heavy", record["configuration_signature"]
            ),
            [],
        )

    def test_validation_rejects_dynamic_balance_or_configuration_changes(self) -> None:
        run, summary = self.heavy_run()
        record = extract_run_record(run, summary, "run.json")
        record["dynamic_balance_enabled_at_start"] = True
        failures = validate_comparison_record(
            record, 1001, "heavy", "hash-heavy", "different"
        )
        self.assertIn("dynamic_balance_enabled_at_start", failures)
        self.assertIn("configuration_signature_changed", failures)

    def test_resume_reuses_only_condition_and_integrity_matching_records(self) -> None:
        run, summary = self.heavy_run()
        record = extract_run_record(run, summary, "run.json")
        request = {
            "seeds": [1001],
            "build_profiles": ["heavy"],
            "player_level_requested": "intermediate",
            "validation_variant": "fixed",
            "valid_runs_per_build": 1,
            "dynamic_balance": False,
        }
        signature = validate_resume_inputs(
            request,
            [record],
            (1001,),
            ("heavy",),
            {"heavy": "hash-heavy"},
        )
        self.assertEqual(signature, record["configuration_signature"])

        request["dynamic_balance"] = True
        with self.assertRaisesRegex(RuntimeError, "does not match"):
            validate_resume_inputs(
                request,
                [record],
                (1001,),
                ("heavy",),
                {"heavy": "hash-heavy"},
            )

    def test_writes_build_and_paired_seed_outputs(self) -> None:
        run, summary = self.heavy_run()
        record = extract_run_record(run, summary, "run.json")
        summaries = aggregate_build_records([record])
        self.assertEqual(summaries["heavy"]["sample_count"], 1)
        self.assertEqual(summaries["heavy"]["win_rate"], 1.0)
        rows, paired = paired_seed_metrics([record])
        self.assertEqual(rows[0]["heavy_final_hp"], 45)
        self.assertEqual(paired["complete_seed_count"], 0)
        with tempfile.TemporaryDirectory() as folder:
            destination = Path(folder)
            write_comparison_outputs(destination, [record], [])
            self.assertTrue((destination / "all_runs.jsonl").exists())
            self.assertTrue((destination / "errors_and_exclusions.jsonl").exists())
            self.assertTrue((destination / "seed_build_comparison.csv").exists())
            report = json.loads((destination / "build_summaries.json").read_text(encoding="utf-8"))
            self.assertEqual(report["builds_summary"]["heavy"]["sample_count"], 1)

    def test_mcp_tool_error_rechecks_state_before_returning(self) -> None:
        error = SimpleNamespace(
            isError=True,
            content=[SimpleNamespace(text="timeout")],
            structuredContent=None,
        )
        state = SimpleNamespace(
            isError=False,
            content=[],
            structuredContent={"result": {"scene": "stage_select"}},
        )
        session = SimpleNamespace(call_tool=AsyncMock(side_effect=[error, state]))
        result = asyncio.run(call(session, "start_new_run", {"run_seed": 1001}))
        self.assertEqual(
            [entry.args[0] for entry in session.call_tool.await_args_list],
            ["start_new_run", "get_game_state"],
        )
        self.assertEqual(result["state_after_error"]["scene"], "stage_select")

    def _comparison_args(
        self,
        output: Path,
        balance_logs: Path,
        seeds: list[int],
        builds: list[str] | None = None,
    ) -> SimpleNamespace:
        profiles_path = Path(__file__).resolve().parent / "build_profiles.json"
        profiles, _ = load_build_profiles(profiles_path)
        return SimpleNamespace(
            profile="intermediate",
            resume_current=False,
            validation_variant="fixed",
            comparison_seed=seeds,
            comparison_build=builds or ["standard"],
            valid_runs_per_build=len(seeds),
            output_directory=output,
            balance_log_directory=balance_logs,
            loaded_build_profiles=profiles,
            build_profiles=profiles_path,
            url="http://127.0.0.1:8765/mcp",
            maximum_steps=10,
            run_log_timeout=0.1,
        )

    def test_exhausted_connection_excludes_only_attempt_and_retries_same_seed(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            output = root / "comparison"
            logs = root / "logs"
            logs.mkdir()
            args = self._comparison_args(output, logs, [1001, 1002])
            play_calls = 0

            async def fake_resilient(url, name, operation, **kwargs):
                nonlocal play_calls
                if name == "configure_player_level":
                    state = {"mcp_control": {"player_level": "intermediate"}}
                    return {"ok": True}, state
                if name == "play_current_run":
                    play_calls += 1
                    if play_calls == 1:
                        raise McpRetryExhausted(
                            name, 5, ConnectionError("offline")
                        )
                    seed = 1001 if play_calls == 2 else 1002
                    return {
                        "completed": True,
                        "reason": "final_boss_defeated",
                        "run_seed": seed,
                    }
                if name == "recover_after_retry_exhaustion":
                    return {"scene": "title"}
                return {"ok": True}

            def fake_record(run, summary, path):
                return {
                    "run_seed": summary["run_seed"],
                    "build_profile": "standard",
                    "configuration_signature": "same-config",
                }

            with patch(
                "collect_fixed_balance_runs.run_mcp_operation_with_retries",
                new=fake_resilient,
            ), patch(
                "collect_fixed_balance_runs._wait_for_run_log",
                new_callable=AsyncMock,
                return_value=(Path("run.json"), {}),
            ), patch(
                "collect_fixed_balance_runs.extract_run_record",
                side_effect=fake_record,
            ), patch(
                "collect_fixed_balance_runs.validate_comparison_record",
                return_value=[],
            ), patch(
                "collect_fixed_balance_runs.write_comparison_outputs"
            ):
                asyncio.run(collect_comparison_suite(args))

            accepted = [
                json.loads(line)
                for line in (output / "all_runs.jsonl").read_text(
                    encoding="utf-8"
                ).splitlines()
            ]
            events = [
                json.loads(line)
                for line in (output / "errors_and_exclusions.jsonl").read_text(
                    encoding="utf-8"
                ).splitlines()
            ]
            self.assertEqual([item["run_seed"] for item in accepted], [1001, 1002])
            self.assertEqual(play_calls, 3)
            self.assertEqual(len(events), 1)
            self.assertTrue(events[0]["excluded"])
            self.assertEqual(events[0]["seed"], 1001)
            self.assertEqual(events[0]["action"], "discard_then_retry_same_seed")
            status = json.loads(
                (output / "comparison_status.json").read_text(encoding="utf-8")
            )
            self.assertEqual(status["state"], "completed")
            self.assertEqual(status["completed_valid_runs"], 2)
            self.assertEqual(status["excluded_runs"], 1)

    def test_small_standard_bounce_seed_matrix_completes(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            output = root / "comparison"
            logs = root / "logs"
            logs.mkdir()
            args = self._comparison_args(
                output, logs, [1001, 1002], ["standard", "bounce"]
            )
            pairs = [
                ("standard", 1001),
                ("standard", 1002),
                ("bounce", 1001),
                ("bounce", 1002),
            ]
            play_index = 0
            record_index = 0

            async def fake_resilient(url, name, operation, **kwargs):
                nonlocal play_index
                if name == "configure_player_level":
                    state = {"mcp_control": {"player_level": "intermediate"}}
                    return {"ok": True}, state
                if name == "play_current_run":
                    build, seed = pairs[play_index]
                    play_index += 1
                    return {
                        "completed": True,
                        "reason": "final_boss_defeated",
                        "run_seed": seed,
                        "build_profile": build,
                    }
                return {"ok": True}

            def fake_record(run, summary, path):
                nonlocal record_index
                build, seed = pairs[record_index]
                record_index += 1
                return {
                    "run_seed": seed,
                    "build_profile": build,
                    "configuration_signature": "same-config",
                }

            with patch(
                "collect_fixed_balance_runs.run_mcp_operation_with_retries",
                new=fake_resilient,
            ), patch(
                "collect_fixed_balance_runs._wait_for_run_log",
                new_callable=AsyncMock,
                return_value=(Path("run.json"), {}),
            ), patch(
                "collect_fixed_balance_runs.extract_run_record",
                side_effect=fake_record,
            ), patch(
                "collect_fixed_balance_runs.validate_comparison_record",
                return_value=[],
            ), patch(
                "collect_fixed_balance_runs.write_comparison_outputs"
            ):
                asyncio.run(collect_comparison_suite(args))

            accepted = [
                json.loads(line)
                for line in (output / "all_runs.jsonl").read_text(
                    encoding="utf-8"
                ).splitlines()
            ]
            self.assertEqual(
                [(item["build_profile"], item["run_seed"]) for item in accepted],
                pairs,
            )

    def test_resume_skips_already_accepted_seed_build_pair(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            output = root / "comparison"
            logs = root / "logs"
            output.mkdir()
            logs.mkdir()
            args = self._comparison_args(output, logs, [1001, 1002])
            saved_record = {
                "run_seed": 1001,
                "build_profile": "standard",
                "configuration_signature": "same-config",
            }
            (output / "all_runs.jsonl").write_text(
                json.dumps(saved_record) + "\n", encoding="utf-8"
            )
            (output / "comparison_request.json").write_text(
                json.dumps({
                    "seeds": [1001, 1002],
                    "build_profiles": ["standard"],
                    "player_level_requested": "intermediate",
                    "validation_variant": "fixed",
                    "valid_runs_per_build": 2,
                    "dynamic_balance": False,
                }),
                encoding="utf-8",
            )
            starts = 0

            async def fake_resilient(url, name, operation, **kwargs):
                nonlocal starts
                if name == "configure_player_level":
                    state = {"mcp_control": {"player_level": "intermediate"}}
                    return {"ok": True}, state
                if name == "start_or_resume_run":
                    starts += 1
                    return {"ok": True}
                if name == "play_current_run":
                    return {
                        "completed": True,
                        "reason": "final_boss_defeated",
                        "run_seed": 1002,
                    }
                return {"ok": True}

            with patch(
                "collect_fixed_balance_runs.validate_resume_inputs",
                return_value="same-config",
            ), patch(
                "collect_fixed_balance_runs.run_mcp_operation_with_retries",
                new=fake_resilient,
            ), patch(
                "collect_fixed_balance_runs._wait_for_run_log",
                new_callable=AsyncMock,
                return_value=(Path("run.json"), {}),
            ), patch(
                "collect_fixed_balance_runs.extract_run_record",
                return_value={
                    "run_seed": 1002,
                    "build_profile": "standard",
                    "configuration_signature": "same-config",
                },
            ), patch(
                "collect_fixed_balance_runs.validate_comparison_record",
                return_value=[],
            ), patch(
                "collect_fixed_balance_runs.write_comparison_outputs"
            ):
                asyncio.run(collect_comparison_suite(args))

            accepted = [
                json.loads(line)
                for line in (output / "all_runs.jsonl").read_text(
                    encoding="utf-8"
                ).splitlines()
            ]
            self.assertEqual(starts, 1)
            self.assertEqual([item["run_seed"] for item in accepted], [1001, 1002])

    def test_failed_status_preserves_pair_retry_and_game_position(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            _write_comparison_status(
                output,
                "failed",
                [],
                [],
                ("bounce",),
                current_build="bounce",
                current_seed=1016,
                error="connection failed",
                retry_count=5,
                retries=7,
                last_error={
                    "exception_type": "ConnectError",
                    "exception_message": "All connection attempts failed",
                    "timestamp": "2026-09-23T00:00:00Z",
                },
                diagnostic_state={
                    "scene": "battle",
                    "game_state": "aiming_direction",
                    "player": {"progress": 9},
                    "balance_validation": {
                        "cleared_stage_count": 7,
                        "run_seed": 1016,
                    },
                    "mcp_control": {"build_profile": {"id": "bounce"}},
                },
            )
            status = json.loads(
                (output / "comparison_status.json").read_text(encoding="utf-8")
            )
            self.assertEqual(status["failed_build"], "bounce")
            self.assertEqual(status["failed_seed"], 1016)
            self.assertEqual(status["scene"], "battle")
            self.assertEqual(status["battle_state"], "aiming_direction")
            self.assertEqual(status["area_progress"], 9)
            self.assertEqual(status["cleared_stage_count"], 7)
            self.assertEqual(status["retry_count"], 5)
            self.assertEqual(status["exception_type"], "ConnectError")


if __name__ == "__main__":
    unittest.main()
