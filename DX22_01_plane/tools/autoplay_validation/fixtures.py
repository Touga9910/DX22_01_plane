"""Prepare copied game data for two real-autoplay battles; source assets stay intact."""
from __future__ import annotations

from pathlib import Path
import shutil
from typing import Any

from .io import digest, read_json, require, write_json

SOURCE_ROOT = Path(__file__).resolve().parents[2]
REPO_ROOT = SOURCE_ROOT.parent
CASES = {"terminal_stop": True, "maximum_runs": False}


def _configure_autoplay(data: Path, stop_at_result: bool, seed: int, decision_frames: int, runs: int) -> None:
    autoplay = read_json(data / "balance_autoplay.json")
    autoplay.update({"enabled": True, "restart_after_game_over": True,
                     "stop_after_current_run": stop_at_result, "max_runs": runs,
                     "decision_delay_frames": decision_frames, "random_seed": seed})
    write_json(data / "balance_autoplay.json", autoplay)
    validation = read_json(data / "balance_validation.json")
    validation.update({
        "enabled": True, "random_seed": seed, "random_seeds": [seed],
        # Retain the historical experiment labels so full normalized logs can
        # be compared with the pre-refactor reference without dropping fields.
        "experiment_id": "autoplay_responsibility_split", "endurance_mode": False,
        "fixed_stage_schedule": False,
        "variants": [{"id": "autoplay_refactor", "adjusted_early_stages": False,
                      "adjusted_mid_stages": False, "normal_play_balance": False,
                      "tactical_ai": False, "tactical_ai_profile": "legacy",
                      "early_attack_budget": 4, "mid_attack_budget": 4}],
    })
    write_json(data / "balance_validation.json", validation)


def _configure_enemies(data: Path) -> None:
    enemies = read_json(data / "enemy_data.json")
    definitions = {enemy["id"]: enemy for enemy in enemies["enemies"]}
    require({"enemy_normal", "enemy_tank"}.issubset(definitions), "Fixture uses existing enemy IDs")
    easy, fatal = definitions["enemy_normal"], definitions["enemy_tank"]
    easy["status"].update({"maxHp": 1, "attack": 0, "defense": 0})
    easy["canCharge"] = False
    fatal["status"].update({"maxHp": 999, "attack": 999, "defense": 0,
                            "mass": 1000.0, "friction": 0.2})
    fatal["canCharge"] = False
    write_json(data / "enemy_data.json", enemies)


def prepare_fixture(folder: Path, stop_at_result: bool, seed: int, decision_frames: int,
                    maximum_runs: int = 1, *, asset_source: Path | None = None,
                    publish_frames: int = 1) -> dict[str, Any]:
    require(not folder.exists(), f"Use a fresh isolated directory: {folder}")
    require(isinstance(stop_at_result, bool), "Stop-at-result control is boolean")
    require(all(isinstance(value, int) and not isinstance(value, bool)
                for value in (seed, decision_frames, maximum_runs, publish_frames)), "Fixture numbers are integers")
    require(0 <= seed <= 0xffffffff and decision_frames >= 1 and maximum_runs >= 1,
            "Fixture seed, decision interval and run count are valid")
    require(not stop_at_result or maximum_runs == 1, "Terminal-stop fixture ends after one run")
    require(1 <= publish_frames <= 60, "State publication interval is bounded")
    folder.mkdir(parents=True, exist_ok=False)
    shutil.copytree(asset_source or SOURCE_ROOT / "assets", folder / "assets")
    shutil.copytree(SOURCE_ROOT / "shader", folder / "shader")
    for path in SOURCE_ROOT.glob("*.cso"):
        shutil.copy2(path, folder / path.name)
    data = folder / "assets/data"
    _configure_autoplay(data, stop_at_result, seed, decision_frames, maximum_runs)
    _configure_enemies(data)
    write_json(data / "game_mcp_bridge.json", {
        "enabled": True, "allow_write_actions": True,
        "bridge_directory": "runtime/game_mcp", "state_publish_interval_frames": publish_frames,
        "command_poll_interval_frames": 1,
    })
    write_json(folder / "saves/settings.json", {
        "bgm_volume": 0, "se_volume": 0, "fullscreen": False,
        "resolution_index": 0, "camera_shake_enabled": False,
    })
    return {
        "stop_after_current_run": stop_at_result, "maximum_runs": maximum_runs,
        "decision_delay_frames": decision_frames, "run_seed": seed,
        "easy_enemy_id": "enemy_normal", "fatal_enemy_id": "enemy_tank",
        "fatal_enemy_note": "Normal difficulty scaling clamps HP/attack to existing limits; high mass prevents an accidental pocket clear.",
        "configuration_sha256": {path.name: digest(path) for path in sorted(data.glob("*.json"))},
        "coverage": ["C++ title start", "route", "ball selection", "normal shot RNG",
                     "clear reward", "second battle", "terminal flags"],
        "coverage_limit": "The first two map areas contain battle nodes, so this fixture does not exercise shop/rest decisions or boss shots.",
    }
