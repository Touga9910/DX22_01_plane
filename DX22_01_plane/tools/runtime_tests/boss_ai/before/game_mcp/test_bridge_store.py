from __future__ import annotations

import json
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from bridge_store import (
    GameBridgeError,
    GameBridgeStore,
    GameCommandBusyError,
    GameNotRunningError,
)


class GameBridgeStoreTests(unittest.TestCase):
    def make_store(self, root: Path) -> GameBridgeStore:
        return GameBridgeStore(
            root,
            command_timeout_seconds=0.5,
        )

    @staticmethod
    def write_state(root: Path, running: bool = True) -> None:
        root.mkdir(parents=True, exist_ok=True)
        (root / "game_state.json").write_text(
            json.dumps(
                {
                    "running": running,
                    "published_at_unix_ms": int(
                        time.time() * 1000
                    ),
                    "scene": "battle",
                    "available_actions": ["fire_shot"],
                    "bridge": {
                        "write_actions_enabled": True,
                    },
                }
            ),
            encoding="utf-8",
        )

    def test_read_state_requires_running_game(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            store = self.make_store(root)
            with self.assertRaises(GameNotRunningError):
                store.read_state()

            self.write_state(root, running=False)
            with self.assertRaises(GameNotRunningError):
                store.read_state()

    def test_stale_state_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.write_state(root)
            state_path = root / "game_state.json"
            state = json.loads(
                state_path.read_text(encoding="utf-8")
            )
            state["published_at_unix_ms"] = 1
            state_path.write_text(
                json.dumps(state),
                encoding="utf-8",
            )

            with self.assertRaises(GameNotRunningError):
                self.make_store(root).read_state()

    def test_read_state_retries_during_file_replacement(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            store = self.make_store(root)

            def publish_state() -> None:
                time.sleep(0.05)
                self.write_state(root)

            game_thread = threading.Thread(target=publish_state)
            game_thread.start()
            state = store.read_state()
            game_thread.join()

            self.assertTrue(state["running"])

    def test_submit_command_waits_for_matching_result(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.write_state(root)
            store = self.make_store(root)

            def simulate_game() -> None:
                command_path = root / "pending_command.json"
                deadline = time.monotonic() + 0.4
                while (
                    not command_path.exists()
                    and time.monotonic() < deadline
                ):
                    time.sleep(0.01)
                command = json.loads(
                    command_path.read_text(encoding="utf-8")
                )
                (root / "last_result.json").write_text(
                    json.dumps(
                        {
                            "command_id": command["command_id"],
                            "action": command["action"],
                            "ok": True,
                            "message": "done",
                        }
                    ),
                    encoding="utf-8",
                )
                command_path.unlink()

            game_thread = threading.Thread(target=simulate_game)
            game_thread.start()
            result = store.submit_command(
                "fire_shot",
                {
                    "direction_x": 1.0,
                    "direction_z": 0.0,
                    "power": 4.0,
                },
            )
            game_thread.join()

            self.assertTrue(result["ok"])
            self.assertEqual(result["action"], "fire_shot")

    def test_submit_command_survives_transient_result_read_error(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.write_state(root)
            store = self.make_store(root)
            original_read_json = store._read_json
            result_read_failed = False

            def flaky_read_json(
                path: Path,
                allow_retry: bool = False,
            ) -> object:
                nonlocal result_read_failed
                if (
                    path == store.result_path
                    and not result_read_failed
                ):
                    result_read_failed = True
                    raise GameBridgeError("temporary read failure")
                return original_read_json(path, allow_retry)

            store._read_json = flaky_read_json  # type: ignore[method-assign]

            def simulate_game() -> None:
                command_path = root / "pending_command.json"
                deadline = time.monotonic() + 0.4
                while (
                    not command_path.exists()
                    and time.monotonic() < deadline
                ):
                    time.sleep(0.01)
                command = json.loads(
                    command_path.read_text(encoding="utf-8")
                )
                (root / "last_result.json").write_text(
                    json.dumps(
                        {
                            "command_id": command["command_id"],
                            "action": command["action"],
                            "ok": True,
                            "message": "done",
                        }
                    ),
                    encoding="utf-8",
                )
                command_path.unlink()

            game_thread = threading.Thread(target=simulate_game)
            game_thread.start()
            result = store.submit_command("heal")
            game_thread.join()

            self.assertTrue(result_read_failed)
            self.assertTrue(result["ok"])
            self.assertEqual(result["action"], "heal")

    def test_existing_command_is_not_overwritten(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.write_state(root)
            (root / "pending_command.json").write_text(
                "{}",
                encoding="utf-8",
            )
            store = self.make_store(root)

            with self.assertRaises(GameCommandBusyError):
                store.submit_command("heal")


if __name__ == "__main__":
    unittest.main()
