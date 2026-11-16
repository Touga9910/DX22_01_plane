"""Own one isolated game child and record every deliberate native request.

Importing this module is portable. Win32 is bound only when a Session is created;
the session never enumerates actions against another process or sends input.
"""
from __future__ import annotations

import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import secrets
import subprocess
import threading
import time
import weakref
from typing import Any

from .lifecycle import SHUTDOWN_EVENTS, read_native
from .process_resources import ProcessResources


MAIN_WINDOW_CLASS = "DX22_01_plane_WindowClass"
OWNED_UI_CLASSES = frozenset({MAIN_WINDOW_CLASS, "ImGui Platform"})


class _Win32:
    def __init__(self) -> None:
        if os.name != "nt":
            raise OSError("A live game Session requires Windows")
        self.user = ctypes.WinDLL("user32", use_last_error=True)
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.kernel.GetTickCount64.argtypes = []
        self.kernel.GetTickCount64.restype = ctypes.c_ulonglong
        self.callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        self.user.EnumWindows.argtypes = [self.callback_type, wintypes.LPARAM]
        self.user.EnumWindows.restype = wintypes.BOOL
        self.user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
        self.user.GetWindowThreadProcessId.restype = wintypes.DWORD
        self.user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
        self.user.GetClassNameW.restype = ctypes.c_int
        self.user.IsWindowVisible.argtypes = [wintypes.HWND]
        self.user.IsWindowVisible.restype = wintypes.BOOL
        self.user.GetForegroundWindow.argtypes = []
        self.user.GetForegroundWindow.restype = wintypes.HWND
        self.user.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
        self.user.ShowWindow.restype = wintypes.BOOL
        self.user.PostThreadMessageW.argtypes = [wintypes.DWORD, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        self.user.PostThreadMessageW.restype = wintypes.BOOL

    def tick(self) -> int:
        return self.kernel.GetTickCount64()

    def owner(self, hwnd: int) -> tuple[int, int]:
        pid = wintypes.DWORD()
        tid = self.user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        return pid.value, tid

    def window_class(self, hwnd: int) -> str:
        name = ctypes.create_unicode_buffer(256)
        self.user.GetClassNameW(hwnd, name, len(name))
        return name.value

    def windows(self, process_id: int) -> list[dict[str, Any]]:
        rows = []
        foreground = self.user.GetForegroundWindow()

        @self.callback_type
        def visit(hwnd, _):
            pid, tid = self.owner(hwnd)
            if pid == process_id:
                rows.append({"hwnd": int(hwnd), "pid": pid, "tid": tid,
                             "class": self.window_class(hwnd),
                             "visible": bool(self.user.IsWindowVisible(hwnd)),
                             "foreground": hwnd == foreground})
            return True

        ctypes.set_last_error(0)
        if not self.user.EnumWindows(visit, 0):
            raise ctypes.WinError(ctypes.get_last_error())
        return sorted(rows, key=lambda row: row["hwnd"])

    def hide(self, hwnd: int) -> dict[str, Any]:
        ctypes.set_last_error(0)
        previous = bool(self.user.ShowWindow(hwnd, 0))
        return {"previously_visible": previous, "visible_after": bool(self.user.IsWindowVisible(hwnd)),
                "last_error": ctypes.get_last_error()}

    def post_quit(self, tid: int, wparam: int, lparam: int) -> dict[str, Any]:
        ctypes.set_last_error(0)
        ok = bool(self.user.PostThreadMessageW(tid, 0x12, wparam, lparam))
        return {"ok": ok, "last_error": ctypes.get_last_error()}


class Session:
    """A single Popen child, its recognized windows, and attributable cleanup.

    ``binding`` may be injected for unit tests; a live session uses _Win32.
    Every request is flushed and fsynced before it is sent. Observation failures
    are retained and never authorize operating on a guessed PID, HWND or thread.
    """
    def __init__(self, folder: Path, condition: str, *, cleanup_timeout: float = 10,
                 kill_timeout: float = 5, binding: Any = None, observe_resources: bool = False) -> None:
        if condition not in ("hidden", "legacy_rehide"):
            raise ValueError("condition must be hidden or legacy_rehide")
        if cleanup_timeout <= 0 or kill_timeout <= 0:
            raise ValueError("cleanup timeouts must be positive")
        self.folder = Path(folder).resolve()
        self.condition = condition
        self.cleanup_timeout, self.kill_timeout = cleanup_timeout, kill_timeout
        self._native = binding if binding is not None else _Win32()
        self.process: subprocess.Popen | None = None
        self.events: list[dict[str, Any]] = []
        self.windows: list[dict[str, Any]] = []
        self.last_windows: list[dict[str, Any]] | None = None
        self.visible_samples = 0
        self.wparam = 0x44000000 | secrets.randbits(24)
        self.lparam = 0x12000000 | secrets.randbits(24)
        self.trace_errors: list[str] = []
        self._cleanup_result: dict[str, Any] | None = None
        self._observe_resources = observe_resources
        self._resources = None

    def record(self, event: str, *, _retain: bool = True, **details: Any) -> dict[str, Any]:
        target_pid = self.process.pid if self.process is not None else None
        row = {"event": event, "phase": details.pop("phase", "observation"),
               "unix_ms": time.time_ns() // 1_000_000, "uptime_ms": self._native.tick(),
               "monotonic_seconds": time.monotonic(), **details,
               "pid": target_pid, "target_pid": target_pid,
               "controller_pid": os.getpid(), "controller_tid": threading.get_native_id()}
        if _retain:
            self.events.append(row)
        try:
            self.folder.mkdir(parents=True, exist_ok=True)
            with (self.folder / "request_events.jsonl").open("a", encoding="utf-8") as stream:
                stream.write(json.dumps(row, ensure_ascii=False) + "\n")
                stream.flush()
                os.fsync(stream.fileno())
        except OSError as error:
            self.trace_errors.append(f"{event}: {type(error).__name__}: {error}")
            raise
        return row

    def _record_safe(self, event: str, **details: Any) -> dict[str, Any] | None:
        try:
            return self.record(event, **details)
        except Exception as error:
            description = f"{event}: {type(error).__name__}: {error}"
            if description not in self.trace_errors:
                self.trace_errors.append(description)
            return None

    def launch(self, command, stdout, **options) -> subprocess.Popen:
        if self.process is not None:
            raise RuntimeError("A Session can launch only one child")
        if options.get("shell"):
            raise ValueError("A Session launches an explicit executable without a shell")
        if "cwd" in options and Path(options["cwd"]).resolve() != self.folder:
            raise ValueError("Session child CWD must be its isolated folder")
        environment = dict(options.pop("env", os.environ))
        if self.condition == "hidden":
            environment["DX22_TEST_HIDDEN_WINDOW"] = "1"
        else:
            environment.pop("DX22_TEST_HIDDEN_WINDOW", None)
        options.update(cwd=self.folder, env=environment, stdout=stdout, shell=False)
        options.setdefault("stderr", subprocess.STDOUT)
        if os.name == "nt":
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
            options.setdefault("startupinfo", startup)
            options.setdefault("creationflags", subprocess.CREATE_NO_WINDOW)
        launch_requested = self.record("launch_requested", phase="launch", condition=self.condition,
                    command=[os.fspath(part) for part in command] if not isinstance(command, str) else command)
        try:
            self.process = subprocess.Popen(command, **options)
        except Exception as error:
            self._record_safe("launch_failed", phase="launch", error=f"{type(error).__name__}: {error}")
            raise
        try:
            launch_completed = self.record("launch_completed", phase="launch", wparam_nonce=self.wparam, lparam_nonce=self.lparam)
            if self._observe_resources:
                record_ref = weakref.WeakMethod(self.record)
                def record_resource(event, **fields):
                    record = record_ref()
                    if record is None:
                        raise ReferenceError("Resource record owner no longer exists")
                    return record(event, _retain=False, **fields)
                self._resources = ProcessResources(self.process, launch_requested, launch_completed,
                    record=record_resource)
        except Exception:
            self.cleanup()
            raise
        return self.process

    def resources_summary(self) -> dict[str, Any]:
        return self._resources.summary() if self._resources is not None else {"enabled": self._observe_resources, "status": "not_launched" if self._observe_resources else "disabled", "samples": 0}

    def _owned(self, hwnd: int, tid: int, classes=OWNED_UI_CLASSES) -> bool:
        return (self.process is not None and self.process.poll() is None
                and self._native.owner(hwnd) == (self.process.pid, tid)
                and self._native.window_class(hwnd) in classes)

    def observe(self) -> list[dict[str, Any]]:
        if self._resources is not None:
            self._resources.sample()
        if self.process is None:
            return []
        rows = self._native.windows(self.process.pid) if self.process.poll() is None else []
        # The native binding filters PID; repeat the check at this boundary too.
        rows = [row for row in rows if row.get("pid") == self.process.pid]
        for row in rows:
            if row["visible"]:
                self.visible_samples += 1
            if (self.condition == "legacy_rehide" and row["visible"]
                    and row["class"] in OWNED_UI_CLASSES and self._owned(row["hwnd"], row["tid"])):
                self.record("rehide_owned_window", hwnd=row["hwnd"], tid=row["tid"], window_class=row["class"])
                if self._owned(row["hwnd"], row["tid"]):
                    result = self._native.hide(row["hwnd"])
                    self.record("rehide_owned_window_completed", hwnd=row["hwnd"], tid=row["tid"], **result)
                else:
                    self.record("rehide_owned_window_aborted", hwnd=row["hwnd"], tid=row["tid"],
                                reason="ownership_changed_after_durable_trace")
        if rows != self.last_windows:
            self.windows.append({"uptime_ms": self._native.tick(), "windows": rows})
            self.last_windows = rows
        return rows

    def main_window(self, process=None) -> tuple[int, int] | None:
        if process is not None and process is not self.process:
            raise ValueError("Window lookup must target this Session's Popen child")
        for row in self.observe():
            if row["class"] == MAIN_WINDOW_CLASS and self._owned(row["hwnd"], row["tid"], {MAIN_WINDOW_CLASS}):
                return row["hwnd"], row["tid"]
        return None

    def read_native(self, *, final: bool = False) -> list[dict[str, Any]]:
        return read_native(self.folder, final=final)

    def cleanup(self, process=None) -> dict[str, Any]:
        if process is not None and process is not self.process:
            raise ValueError("Cleanup must target this Session's Popen child")
        if self._cleanup_result is not None:
            return self._cleanup_result
        child = self.process
        result: dict[str, Any] = {"process_id": child.pid if child else None,
                                  "method": "not_launched" if child is None else "already_exited", "requests": []}
        if child is None:
            result["exit_code"] = None
            self._cleanup_result = result
            return result
        errors = []

        def terminate(reason):
            self._record_safe("terminate_owned_child", phase="fixture_finally", reason=reason)
            result["method"] = "terminate_owned_child"
            try:
                child.terminate()
            except Exception as error:
                errors.append(f"terminate: {type(error).__name__}: {error}")

        try:
            poll = child.poll()
            try:
                prior = [row for row in self.read_native() if row.get("event") in SHUTDOWN_EVENTS]
            except Exception as error:
                prior = []
                errors.append(f"native_observation: {type(error).__name__}: {error}")
            self._record_safe("cleanup_entered", phase="fixture_finally", poll=poll, prior_native_shutdown=prior)
            if poll is None:
                if prior:
                    result["method"] = "wait_for_already_started_native_shutdown"
                else:
                    try:
                        window = self.main_window()
                        # Observation and fsync can take time. Recheck immediately
                        # before making a request and preserve any competing exit.
                        recent = [row for row in self.read_native() if row.get("event") in SHUTDOWN_EVENTS]
                        if child.poll() is not None or recent:
                            result["method"] = "wait_for_already_started_native_shutdown"
                            self._record_safe("cleanup_observed_competing_shutdown", phase="fixture_finally", native=recent, poll=child.poll())
                        elif window and self._owned(*window, {MAIN_WINDOW_CLASS}):
                            hwnd, tid = window
                            begin = self.record("wm_quit_send_begin", phase="fixture_finally", hwnd=hwnd, tid=tid,
                                                wparam=self.wparam, lparam=self.lparam)
                            # Durable recording itself takes time. Never trust
                            # a HWND/TID observed before that disk operation.
                            if self._owned(hwnd, tid, {MAIN_WINDOW_CLASS}):
                                posted = self._native.post_quit(tid, self.wparam, self.lparam)
                                end = self._record_safe("wm_quit_send_end", phase="fixture_finally", hwnd=hwnd, tid=tid,
                                                        wparam=self.wparam, lparam=self.lparam, **posted)
                                result["requests"].append({"begin": begin, "end": end})
                                result["method"] = "tagged_WM_QUIT_to_owned_thread"
                                if not posted["ok"] and child.poll() is None:
                                    terminate("post_failed")
                            else:
                                self._record_safe("wm_quit_send_aborted", phase="fixture_finally", hwnd=hwnd, tid=tid,
                                                  reason="ownership_changed_after_durable_trace", poll=child.poll())
                                result["method"] = "wait_after_ownership_changed"
                        elif child.poll() is None:
                            terminate("no_owned_main_window")
                    except Exception as error:
                        errors.append(f"cleanup_request: {type(error).__name__}: {error}")
                        if child.poll() is None:
                            terminate("cleanup_request_error")
                try:
                    child.wait(timeout=self.cleanup_timeout)
                except subprocess.TimeoutExpired:
                    self._record_safe("kill_owned_child", phase="fixture_finally", reason="shutdown_timeout")
                    result["method"] = "kill_after_shutdown_timeout"
                    child.kill()
                    child.wait(timeout=self.kill_timeout)
        except Exception as error:
            errors.append(f"cleanup: {type(error).__name__}: {error}")
        finally:
            try:
                self.observe()
            except Exception as error:
                errors.append(f"final_window_observation: {type(error).__name__}: {error}")
            result["exit_code"] = child.poll()
            result["errors"] = errors
            self._record_safe("cleanup_completed", phase="fixture_finally", exit_code=result["exit_code"],
                              method=result["method"], errors=errors)
            result["trace_errors"] = list(self.trace_errors)
            self._cleanup_result = result
        return result
