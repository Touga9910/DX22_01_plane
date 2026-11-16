"""Real C++ autoplay scenarios, independent of process launch and cleanup."""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import json
import time
from typing import Any, Callable, TextIO

from game_mcp.bridge_store import GameBridgeError, GameBridgeStore

from .io import require, write_json


@dataclass(frozen=True)
class ScenarioOptions:
    case: str
    seed: int
    expected_runs: int = 1
    timeout_seconds: float = 180.0


class ScenarioDriver:
    def __init__(self, folder: Path, process: Any, observe: Callable[[], Any], trace: TextIO,
                 options: ScenarioOptions):
        self.folder, self.process, self.observe, self.trace = folder, process, observe, trace
        self.options = options
        self.deadline = time.monotonic() + options.timeout_seconds
        self.store = GameBridgeStore(folder / "runtime/game_mcp", command_timeout_seconds=15)
        self.latest_state: dict[str, Any] = {}
        self.last_sequence: Any = None
        self.milestones: list[dict[str, Any]] = []
        self.commands: list[dict[str, Any]] = []

    def read_state(self) -> dict[str, Any] | None:
        require(self.process.poll() is None, f"Fixture process exited unexpectedly: {self.process.returncode}")
        self.observe()
        try:
            state = self.store.read_state()
        except GameBridgeError:
            return None
        self.latest_state = state
        if state.get("sequence") != self.last_sequence:
            self.trace.write(json.dumps(state, ensure_ascii=False, separators=(",", ":")) + "\n")
            self.trace.flush()
            self.last_sequence = state.get("sequence")
        return state

    def wait_for(self, name: str, predicate: Callable[[dict[str, Any]], bool]) -> dict[str, Any]:
        while time.monotonic() < self.deadline:
            state = self.read_state()
            if state is not None and predicate(state):
                write_json(self.folder / f"state_{name}.json", state)
                self.milestones.append({"name": name, "scene": state.get("scene"),
                    "game_state": state.get("game_state"), "autoplay_enabled": state.get("autoplay_enabled"),
                    "stop_after_current_run": state.get("autoplay_stop_after_current_run")})
                print(json.dumps({"case": self.options.case, "milestone": name, "scene": state.get("scene")}), flush=True)
                return state
            time.sleep(0.05)
        raise TimeoutError(f"Case exceeded {self.options.timeout_seconds}s while waiting for {name}")

    def queue_layout(self, name: str, enemy_id: str, x: float, z: float, prefix: str) -> None:
        remaining = self.deadline - time.monotonic()
        require(remaining > 0, "Runtime deadline remains before queueing a layout")
        self.store.command_timeout_seconds = min(15.0, remaining)
        arguments = {"layout_id": name, "stage_type": "normal", "difficulty": 1, "par": 4,
                     "enemies": [{"enemy_id": enemy_id, "x": x, "z": z}]}
        result = self.store.submit_command("set_next_stage_layout", arguments)
        self.commands.append({"action": "set_next_stage_layout", "arguments": arguments, "result": result})
        require(result.get("ok") is True, f"Layout queue rejected: {result}")
        state = self.wait_for(prefix + name + "_queued", lambda state: (
            state.get("stage_layout_control", {}).get("queued_override") or {}).get("layout_id") == name)
        require(state.get("autoplay_enabled") is True, "Layout command preserves C++ autoplay")

    def play_run(self, index: int) -> dict[str, Any]:
        prefix = "" if self.options.expected_runs == 1 else f"run_{index:03d}_"
        state = self.wait_for(prefix + "first_stage_select", lambda state: state.get("scene") == "stage_select")
        require(state.get("autoplay_enabled") is True, "C++ autoplay starts the run")
        require(state.get("balance_validation", {}).get("run_seed") == self.options.seed, "Validation fixes the run seed")
        self.queue_layout("autoplay_easy", "enemy_normal", 14.0, 8.0, prefix)
        self.wait_for(prefix + "easy_battle", lambda state: state.get("scene") == "battle" and (
            state.get("stage_layout_control", {}).get("current_override") or {}).get("layout_id") == "autoplay_easy")
        self.wait_for(prefix + "first_clear_reward", lambda state: state.get("clear_reward_active") is True)
        self.queue_layout("autoplay_fatal", "enemy_tank", -14.0, 8.0, prefix)
        self.wait_for(prefix + "fatal_battle", lambda state: state.get("scene") == "battle" and (
            state.get("stage_layout_control", {}).get("current_override") or {}).get("layout_id") == "autoplay_fatal")
        return self.wait_for(prefix + "result", lambda state: state.get("scene") == "result")

    def run(self) -> dict[str, Any]:
        require(self.options.case in ("terminal_stop", "maximum_runs"), "Known terminal case")
        require(self.options.expected_runs >= 1, "At least one native run is expected")
        require(self.options.case != "terminal_stop" or self.options.expected_runs == 1,
                "Terminal-stop scenario cannot auto-restart")
        result_state = None
        for index in range(1, self.options.expected_runs + 1):
            result_state = self.play_run(index)
            if self.options.case == "maximum_runs":
                require(result_state.get("autoplay_enabled") is True,
                        "Maximum-runs case reaches Result with autoplay still enabled")
        if self.options.case == "terminal_stop":
            terminal = self.wait_for("terminal_stopped", lambda state: state.get("scene") == "result"
                and state.get("autoplay_enabled") is False
                and state.get("autoplay_stop_after_current_run") is False)
        else:
            terminal = self.wait_for("maximum_runs_stopped", lambda state: state.get("scene") == "title"
                and state.get("autoplay_enabled") is False)
        stable_until = time.monotonic() + 1.0
        if stable_until > self.deadline:
            raise TimeoutError("Full one-second terminal observation no longer fits within deadline")
        while time.monotonic() < stable_until:
            state = self.read_state()
            if state:
                require(state.get("scene") == terminal.get("scene") and state.get("autoplay_enabled") is False,
                        "Stopped autoplay remains at its terminal scene")
            time.sleep(0.05)
        return {"scene": terminal["scene"], "autoplay_enabled": terminal["autoplay_enabled"],
                "stop_after_current_run": terminal["autoplay_stop_after_current_run"]}


def validate_run_logs(runs: list[dict[str, Any]], seed: int, expected_runs: int) -> None:
    require(len(runs) == expected_runs, "Exactly the expected number of native runs were created")
    for run in runs:
        require(run.get("controller_type") == "autoplay", "Captured run belongs to C++ autoplay")
        require(run.get("run_context", {}).get("randomness", {}).get("run_seed") == seed, "Saved run seed is fixed")
        require(run.get("run_result", {}).get("result") == "game_over", "Fatal battle reaches a real game-over result")
        require([stage.get("stage_id") for stage in run.get("stages", [])] == ["autoplay_easy", "autoplay_fatal"],
                "C++ player played both queued stages in order")
        require(all(stage.get("shots") for stage in run["stages"]), "C++ autoplay fired in both battles")
        choices = [event for event in run.get("events", []) if event.get("event_type") == "clear_reward_choice"]
        require(len(choices) == 1 and choices[0].get("details", {}).get("controller") == "autoplay",
                "C++ autoplay selected the first clear reward")
