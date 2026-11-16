"""Observe ordinary local C++ autoplay without injecting gameplay commands.

Enemy/player data, stage layouts, progression, offers and rewards are copied
verbatim. Only execution controls, observation, audio and external automation
are changed in the private fixture directory.
"""
from __future__ import annotations

from pathlib import Path
import shutil
import time
from typing import Any

from .fixtures import SOURCE_ROOT
from .io import digest, read_json, require, write_json


TERMINAL_RUN_RESULTS = frozenset({"game_over", "completed"})
_CONTROL_FILES = frozenset({"balance_autoplay.json", "balance_validation.json",
                            "game_mcp_bridge.json", "auto_balance_loop.json",
                            "balance_ai_openai.json"})
_TOOLS = Path(__file__).resolve().parents[1]


def _evidence(filename: str, text: str) -> dict[str, Any]:
    path = _TOOLS / filename
    lines = path.read_text(encoding="utf-8-sig").splitlines()
    return {"source": "tools/" + filename,
            "line": next(index for index, line in enumerate(lines, 1) if text in line),
            "sha256": digest(path)}


def _update_control(path: Path, updates: dict[str, Any], changes: list[dict[str, Any]],
                    folder: Path, *, required: bool = True) -> None:
    if not path.exists() and not required:
        changes.append({"path": path.relative_to(folder).as_posix(), "present": False,
                        "action": "not_present"})
        return
    existed = path.exists()
    value = read_json(path) if existed else {}
    require(isinstance(value, dict), f"Expected configuration object: {path.name}")
    original = dict(value)
    fields = {name: {"was_present": name in original, "before": original.get(name), "after": after}
              for name, after in updates.items()}
    value.update(updates)
    changed = value != original or not existed
    if changed:
        write_json(path, value)
    changes.append({"path": path.relative_to(folder).as_posix(), "present": existed,
                    "action": "update_execution_controls", "changed": changed, "fields": fields})


def prepare_ordinary_fixture(folder: Path, stop_at_result: bool, seed: int, decision_frames: int,
                             maximum_runs: int = 1, *, publish_frames: int = 1) -> dict[str, Any]:
    """Copy normal data and disable optional external controllers in that copy."""
    folder = Path(folder)
    require(not folder.exists(), f"Use a fresh isolated directory: {folder}")
    require(isinstance(stop_at_result, bool), "Stop-at-result control is boolean")
    require(all(isinstance(value, int) and not isinstance(value, bool)
                for value in (seed, decision_frames, maximum_runs, publish_frames)), "Fixture numbers are integers")
    require(0 <= seed <= 0xFFFFFFFF and decision_frames >= 1 and maximum_runs >= 1,
            "Fixture seed, decision interval and run count are valid")
    require(not stop_at_result or maximum_runs == 1, "Terminal-stop fixture ends after one run")
    require(1 <= publish_frames <= 60, "State publication interval is bounded")
    source = Path(SOURCE_ROOT).resolve()
    require(not folder.resolve().is_relative_to(source / "assets") and
            not folder.resolve().is_relative_to(source / "shader") and folder.resolve() != source,
            "Fixture output must not overwrite or recurse into source assets/shaders")
    folder.mkdir(parents=True, exist_ok=False)
    shutil.copytree(source / "assets", folder / "assets")
    shutil.copytree(source / "shader", folder / "shader")
    for path in source.glob("*.cso"):
        shutil.copy2(path, folder / path.name)
    data = folder / "assets/data"
    original_json = {path.name: digest(path) for path in sorted(data.glob("*.json"))}
    changes: list[dict[str, Any]] = []
    _update_control(data / "balance_autoplay.json", {
        "enabled": True, "restart_after_game_over": True,
        "stop_after_current_run": stop_at_result, "max_runs": maximum_runs,
        "decision_delay_frames": decision_frames, "random_seed": seed,
    }, changes, folder)
    # Ordinary gameplay uses normal stage pools/budgets, not validation variants.
    _update_control(data / "balance_validation.json", {"enabled": False}, changes, folder)
    _update_control(data / "game_mcp_bridge.json", {
        "enabled": True, "allow_write_actions": False, "bridge_directory": "runtime/game_mcp",
        "state_publish_interval_frames": publish_frames, "command_poll_interval_frames": 1,
    }, changes, folder)
    _update_control(data / "auto_balance_loop.json", {"enabled": False}, changes, folder, required=False)

    ai_config = data / "balance_ai_openai.json"
    if ai_config.exists():
        value = read_json(ai_config)
        require(isinstance(value, dict), "AI configuration is an object")
        retired = ai_config.with_name(ai_config.name + ".ordinary-disabled")
        suffix = 1
        while retired.exists():
            retired = ai_config.with_name(ai_config.name + f".ordinary-disabled-{suffix}")
            suffix += 1
        before_hash = digest(ai_config)
        ai_config.rename(retired)
        changes.append({"path": ai_config.relative_to(folder).as_posix(), "present": True,
                        "action": "retire_default_AI_configuration", "provider_before": value.get("provider"),
                        "enabled_was_present": "enabled" in value, "enabled_before": value.get("enabled"),
                        "retired_path": retired.relative_to(folder).as_posix(), "sha256": before_hash,
                        "default_path_exists_after": ai_config.exists(),
                        "scope": "The default Python AI entry point fails loading this missing path before any HTTP call. Explicitly overriding its config is outside this fixture driver."})
    else:
        changes.append({"path": "assets/data/balance_ai_openai.json", "present": False,
                        "action": "not_present"})
    settings = {"bgm_volume": 0, "se_volume": 0, "fullscreen": False}
    write_json(folder / "saves/settings.json", settings)
    changes.append({"path": "saves/settings.json", "action": "create_audio_display_controls",
                    "fields": settings, "other_settings": "Use existing C++ loader defaults"})

    preserved = {name: old for name, old in original_json.items() if name not in _CONTROL_FILES}
    require(all((data / name).is_file() and digest(data / name) == old for name, old in preserved.items()),
            "Ordinary gameplay JSON data is byte-identical to the source copy")
    return {
        "profile": "ordinary", "stop_after_current_run": stop_at_result,
        "maximum_runs": maximum_runs, "decision_delay_frames": decision_frames, "run_seed": seed,
        "run_seed_rule": "(seed + zero_based_run_index) modulo 2^32; C++ validation is disabled",
        "expected_run_seeds": [(seed + index) & 0xFFFFFFFF for index in range(maximum_runs)],
        "configuration_changes": changes,
        "preserved_gameplay_json_sha256": preserved,
        "configuration_sha256": {path.name: digest(path) for path in sorted(data.glob("*.json"))},
        "external_automation": {
            "auto_balance_loop": "Disabled in the copied config; the separate Python run_loop returns 2 before collection/AI.",
            "network_AI": "Default AI config retired in the copy; no enabled:false assumption is used.",
            "driver": "Only the existing game child is observed; no AI client, external balance loop or MCP write command is invoked.",
            "evidence": {
                "AI_default_path": _evidence("generate_balance_plan_with_ai.py", "DEFAULT_AI_CONFIG_FILE ="),
                "AI_config_load_before_HTTP": _evidence("generate_balance_plan_with_ai.py", "ai_config = load_json(args.ai_config)"),
                "missing_config_has_no_fallback": _evidence("analyze_balance_logs.py", 'with path.open("r", encoding="utf-8") as source:'),
                "AI_missing_file_caught_as_failure": _evidence("generate_balance_plan_with_ai.py", "Failed to generate AI adjustment plan:"),
                "external_loop_enabled_gate": _evidence("auto_balance_loop.py", 'if not bool(config.get("enabled", False)):'),
            },
        },
        "accepted_run_results": sorted(TERMINAL_RUN_RESULTS),
        "terminal_result_source": "GameRunCompletion.cpp: CompleteFinalBossRun -> completed; ProcessGameOver -> game_over",
        "commands_expected": 0,
        "coverage": ["ordinary C++ title start", "normal routes and battle data", "native ball and reward decisions",
                     "real GameOver or final-boss completion", "normal autoplay terminal flags"],
        "coverage_limit": "No fixed number of battles or guaranteed rest/shop/boss encounter. A time limit before terminal completion is fixture_timeout, not a spontaneous process exit.",
    }


def _state_seed(state: dict[str, Any]) -> Any:
    return state.get("balance_validation", {}).get("run_seed")


def run_ordinary(driver: Any) -> dict[str, Any]:
    """Use ScenarioDriver observation/trace methods; never submit commands."""
    options = driver.options
    require(options.case in ("terminal_stop", "maximum_runs"), "Known ordinary terminal case")
    require(options.expected_runs >= 1, "At least one ordinary native run is expected")
    require(options.case != "terminal_stop" or options.expected_runs == 1,
            "Terminal-stop scenario cannot auto-restart")
    require(not driver.commands, "Ordinary observation starts with no gameplay commands")
    for index in range(1, options.expected_runs + 1):
        expected_seed = (options.seed + index - 1) & 0xFFFFFFFF
        prefix = f"ordinary_run_{index:03d}_"
        if index > 1:
            left = driver.wait_for(prefix + "left_previous_result", lambda state: state.get("scene") != "result")
            require(left.get("autoplay_enabled") is True, "C++ autoplay restarts before its run limit")
        driver.wait_for(prefix + "started", lambda state: state.get("scene") not in (None, "title", "result")
                        and state.get("autoplay_enabled") is True and _state_seed(state) == expected_seed
                        and state.get("balance_validation", {}).get("enabled") is False)
        result = driver.wait_for(prefix + "result", lambda state: state.get("scene") == "result"
                                 and _state_seed(state) == expected_seed)
        if options.case == "maximum_runs":
            require(result.get("autoplay_enabled") is True,
                    "Maximum-runs case reaches Result before autoplay stops at Title")
    last_seed = (options.seed + options.expected_runs - 1) & 0xFFFFFFFF
    if options.case == "terminal_stop":
        terminal = driver.wait_for("ordinary_terminal_stopped", lambda state: state.get("scene") == "result"
                                   and state.get("autoplay_enabled") is False
                                   and state.get("autoplay_stop_after_current_run") is False
                                   and _state_seed(state) == last_seed)
    else:
        terminal = driver.wait_for("ordinary_maximum_runs_stopped", lambda state: state.get("scene") == "title"
                                   and state.get("autoplay_enabled") is False and _state_seed(state) == last_seed)
    stable_until = time.monotonic() + 1.0
    if stable_until > driver.deadline:
        raise TimeoutError("Full one-second ordinary terminal observation does not fit within deadline")
    while time.monotonic() < stable_until:
        state = driver.read_state()
        if state:
            require(state.get("scene") == terminal.get("scene") and state.get("autoplay_enabled") is False
                    and _state_seed(state) == last_seed, "Ordinary autoplay remains stopped at its terminal scene")
        time.sleep(0.05)
    require(not driver.commands, "Ordinary autoplay issued zero gameplay commands")
    return {"scene": terminal["scene"], "autoplay_enabled": terminal["autoplay_enabled"],
            "stop_after_current_run": terminal["autoplay_stop_after_current_run"]}


def validate_ordinary_logs(runs: list[dict[str, Any]], seed: int, expected_runs: int) -> None:
    """Accept only real natural terminal logs and the ordinary C++ seed sequence."""
    require(expected_runs >= 1 and len(runs) == expected_runs,
            "Exactly the expected number of ordinary native runs completed")
    for index, run in enumerate(runs):
        expected_seed = (seed + index) & 0xFFFFFFFF
        require(run.get("controller_type") == "autoplay", "Ordinary run belongs to C++ autoplay")
        context = run.get("run_context", {})
        require(context.get("randomness", {}).get("run_seed") == expected_seed,
                "Ordinary run uses the C++ autoplay seed plus run index")
        require(context.get("validation", {}).get("enabled") is False,
                "Ordinary run does not use balance-validation overrides")
        require(run.get("run_result", {}).get("result") in TERMINAL_RUN_RESULTS,
                "Ordinary run ends with game_over or completed, not cleanup/application_exit")
        stages = run.get("stages", [])
        require(isinstance(stages, list) and stages, "Ordinary run contains native battle stages")
        require(any(stage.get("shots") for stage in stages), "C++ autoplay fired a native shot")
