from __future__ import annotations

import json
import math
import os
import threading
import time
import uuid
from pathlib import Path
from typing import Any


READ_RETRY_DELAYS_SECONDS = (0.01, 0.02, 0.04, 0.08)


class GameBridgeError(RuntimeError):
    """Base error returned to an MCP tool caller."""


class GameNotRunningError(GameBridgeError):
    """The game has not published a usable live state."""


class GameCommandBusyError(GameBridgeError):
    """A previous state-changing command is still pending."""


class GameBridgeStore:
    def __init__(
        self,
        bridge_directory: Path,
        command_timeout_seconds: float = 15.0,
        maximum_state_age_seconds: float = 10.0,
    ) -> None:
        self.bridge_directory = bridge_directory.resolve()
        self.command_timeout_seconds = max(
            0.1,
            command_timeout_seconds,
        )
        self.maximum_state_age_seconds = max(
            1.0,
            maximum_state_age_seconds,
        )
        self._command_lock = threading.Lock()

    @property
    def state_path(self) -> Path:
        return self.bridge_directory / "game_state.json"

    @property
    def command_path(self) -> Path:
        return self.bridge_directory / "pending_command.json"

    @property
    def result_path(self) -> Path:
        return self.bridge_directory / "last_result.json"

    def read_state(self) -> dict[str, Any]:
        state = self._read_json(
            self.state_path,
            allow_retry=True,
        )
        if not isinstance(state, dict):
            raise GameNotRunningError(
                "ゲーム状態の形式が正しくありません。"
            )
        if not state.get("running", False):
            raise GameNotRunningError(
                "ゲームが起動していないか、終了済みです。"
            )
        published_at = state.get("published_at_unix_ms")
        if not isinstance(published_at, int):
            raise GameNotRunningError(
                "ゲーム状態に更新時刻がありません。"
            )
        state_age_seconds = (
            int(time.time() * 1000) - published_at
        ) / 1000.0
        if state_age_seconds > self.maximum_state_age_seconds:
            raise GameNotRunningError(
                "ゲーム状態の更新が止まっています。"
                "ゲームが応答しているか確認してください。"
            )
        return state

    def submit_command(
        self,
        action: str,
        arguments: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        with self._command_lock:
            state = self.read_state()
            bridge = state.get("bridge", {})
            if not bridge.get("write_actions_enabled", False):
                raise GameBridgeError(
                    "ゲーム側で書き込み操作が無効になっています。"
                )

            if self.command_path.exists():
                raise GameCommandBusyError(
                    "前のゲーム操作がまだ処理中です。"
                )

            command_id = uuid.uuid4().hex
            command = {
                "schema_version": 1,
                "command_id": command_id,
                "action": action,
                "arguments": arguments or {},
                "created_at_unix_ms": int(time.time() * 1000),
            }
            self._write_json_atomically(self.command_path, command)

            deadline = (
                time.monotonic() + self.command_timeout_seconds
            )
            while time.monotonic() < deadline:
                if self.result_path.exists():
                    try:
                        result = self._read_json(
                            self.result_path,
                            allow_retry=True,
                        )
                    except GameBridgeError:
                        # The C++ process may be replacing the result file.
                        # Keep polling until the command deadline instead of
                        # surfacing a transient bridge read as a tool error.
                        result = None
                    if (
                        isinstance(result, dict)
                        and result.get("command_id") == command_id
                    ):
                        return result
                time.sleep(0.05)

            return {
                "schema_version": 1,
                "command_id": command_id,
                "action": action,
                "ok": False,
                "status": "timeout",
                "message": (
                    "ゲームから時間内に応答がありませんでした。"
                    "コマンドは後から実行される可能性があるため、"
                    "get_game_stateで状態を再確認してください。"
                ),
            }

    @staticmethod
    def require_finite_number(
        value: float,
        field_name: str,
    ) -> float:
        converted = float(value)
        if not math.isfinite(converted):
            raise GameBridgeError(
                f"{field_name}には有限の数値を指定してください。"
            )
        return converted

    @staticmethod
    def _read_json(
        path: Path,
        allow_retry: bool = False,
    ) -> Any:
        retry_delays = (
            READ_RETRY_DELAYS_SECONDS
            if allow_retry
            else ()
        )
        attempts = len(retry_delays) + 1
        last_error: Exception | None = None
        for attempt in range(attempts):
            try:
                with path.open("r", encoding="utf-8") as file:
                    return json.load(file)
            except json.JSONDecodeError as error:
                last_error = error
            except OSError as error:
                last_error = error
            if attempt < len(retry_delays):
                time.sleep(retry_delays[attempt])

        if isinstance(last_error, FileNotFoundError):
            raise GameNotRunningError(
                "ゲーム状態ファイルがありません。"
                "先にゲームを起動してください。"
            ) from last_error
        raise GameBridgeError(
            f"JSONを読み取れませんでした: {path}"
        ) from last_error

    @staticmethod
    def _write_json_atomically(
        path: Path,
        value: dict[str, Any],
    ) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary_path = path.with_suffix(
            path.suffix + f".{uuid.uuid4().hex}.tmp"
        )
        try:
            with temporary_path.open(
                "w",
                encoding="utf-8",
                newline="\n",
            ) as file:
                json.dump(
                    value,
                    file,
                    ensure_ascii=False,
                    indent=2,
                )
                file.flush()
                os.fsync(file.fileno())
            os.replace(temporary_path, path)
        finally:
            if temporary_path.exists():
                temporary_path.unlink()
