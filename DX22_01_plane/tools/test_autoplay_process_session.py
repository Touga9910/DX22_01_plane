"""Check Session ownership and cleanup boundaries without native processes.

The fake binding represents stale window observations and delayed native exits.
Real temporary JSONL files exercise the request-recording boundary, while Popen
is always replaced before launch is called. No game or desktop UI is used.
"""
from __future__ import annotations

from contextlib import contextmanager
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from autoplay_validation import process_session
from autoplay_validation.process_session import MAIN_WINDOW_CLASS, Session


NO_COMPLETION = object()


class FakeChild:
    def __init__(self, pid: int):
        self.pid = pid
        self.returncode = None
        self.actions: list[tuple] = []
        self.complete_on_wait = NO_COMPLETION

    def poll(self):
        return self.returncode

    def wait(self, timeout):
        self.actions.append(("wait", timeout))
        if self.returncode is None and self.complete_on_wait is not NO_COMPLETION:
            self.returncode = self.complete_on_wait
        if self.returncode is None:
            raise subprocess.TimeoutExpired("fake-owned-child", timeout)
        return self.returncode

    def terminate(self):
        self.actions.append(("terminate",))
        self.returncode = 77

    def kill(self):
        self.actions.append(("kill",))
        self.returncode = 88


class FakeBinding:
    def __init__(self, child: FakeChild):
        self.child = child
        self.tick_count = 1000
        self.rows = [{"hwnd": 101, "pid": child.pid, "tid": 201,
                      "class": MAIN_WINDOW_CLASS, "visible": False, "foreground": False}]
        self.owners = {101: (child.pid, 201)}
        self.classes = {101: MAIN_WINDOW_CLASS}
        self.posts: list[dict] = []
        self.hides: list[int] = []
        self.enumerations: list[int] = []
        self.accept_post = True
        self.exit_on_post = True
        self.before_post = None
        self.before_hide = None

    def tick(self):
        self.tick_count += 1
        return self.tick_count

    def windows(self, pid):
        self.enumerations.append(pid)
        # Deliberately permit stale/foreign rows: Session must filter these too.
        return [dict(row) for row in self.rows]

    def owner(self, hwnd):
        return self.owners.get(hwnd, (0, 0))

    def window_class(self, hwnd):
        return self.classes.get(hwnd, "")

    def hide(self, hwnd):
        if self.before_hide:
            self.before_hide(hwnd)
        self.hides.append(hwnd)
        for row in self.rows:
            if row["hwnd"] == hwnd:
                row["visible"] = False
        return {"previously_visible": True, "visible_after": False, "last_error": 0}

    def post_quit(self, tid, wparam, lparam):
        if self.before_post:
            self.before_post(tid, wparam, lparam)
        self.posts.append({"tid": tid, "wparam": wparam, "lparam": lparam})
        if self.accept_post and self.exit_on_post:
            self.child.returncode = 0
        return {"ok": self.accept_post, "last_error": 0 if self.accept_post else 1444}


class AutoplayProcessSessionTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.folder = Path(temporary.name) / "isolated"
        self.child = FakeChild(12001)
        self.foreign = FakeChild(99001)
        self.binding = FakeBinding(self.child)
        self.session = Session(self.folder, "hidden", binding=self.binding)
        self.session.process = self.child

    def tearDown(self):
        self.assertEqual(self.foreign.actions, [], "Cleanup must never operate on another Popen")
        self.assertTrue(all(pid == self.child.pid for pid in self.binding.enumerations))

    def trace(self):
        path = self.folder / "request_events.jsonl"
        return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()] if path.exists() else []

    def test_visible_condition_observes_without_hiding_the_game(self):
        self.session.condition = "visible"
        self.binding.rows[0]["visible"] = True
        observed = self.session.observe()
        self.assertTrue(observed[0]["visible"])
        self.assertEqual(self.session.visible_samples, 1)
        self.assertEqual(self.binding.hides, [])

    def test_trace_distinguishes_the_controller_from_the_game(self):
        with patch.object(process_session.os, "getpid", return_value=73001), \
                patch.object(process_session.threading, "get_native_id", return_value=83001):
            row = self.session.record("identity_observed")
        self.assertEqual((row["pid"], row["target_pid"]), (self.child.pid, self.child.pid))
        self.assertEqual((row["controller_pid"], row["controller_tid"]), (73001, 83001))
        self.assertEqual(self.trace(), [row])

    def test_trace_before_launch_does_not_invent_a_game_pid(self):
        self.session.process = None
        row = self.session.record("launch_requested", phase="launch")
        self.assertIsNone(row["pid"])
        self.assertIsNone(row["target_pid"])
        self.assertEqual(row["controller_pid"], os.getpid())
        self.assertEqual(row["controller_tid"], process_session.threading.get_native_id())

    def test_request_details_cannot_replace_the_recorded_process_identity(self):
        row = self.session.record("identity_observed", pid=self.foreign.pid, target_pid=self.foreign.pid,
                                  controller_pid=0, controller_tid=0)
        self.assertEqual((row["pid"], row["target_pid"]), (self.child.pid, self.child.pid))
        self.assertEqual(row["controller_pid"], os.getpid())
        self.assertEqual(row["controller_tid"], process_session.threading.get_native_id())

    @contextmanager
    def on_durable_record(self, callback):
        """Run a boundary action while a particular flushed request is fsynced."""
        def fsync(_descriptor):
            callback(self.trace()[-1])

        with patch.object(process_session.os, "fsync", side_effect=fsync):
            yield

    def test_tagged_cleanup_posts_to_owned_thread_after_durable_request(self):
        durable = []

        def before_post(tid, wparam, lparam):
            self.assertEqual(tid, 201)
            self.assertEqual((wparam, lparam), (self.session.wparam, self.session.lparam))
            self.assertIn("wm_quit_send_begin", durable)
            recorded = self.trace()[-1]
            self.assertEqual(recorded["event"], "wm_quit_send_begin")
            self.assertEqual((recorded["pid"], recorded["tid"]), (self.child.pid, tid))
            self.assertEqual((recorded["wparam"], recorded["lparam"]), (wparam, lparam))

        self.binding.before_post = before_post
        with self.on_durable_record(lambda row: durable.append(row["event"])):
            result = self.session.cleanup()
        self.assertEqual(result["exit_code"], 0)
        self.assertEqual(len(self.binding.posts), 1)
        self.assertEqual(result["method"], "tagged_WM_QUIT_to_owned_thread")
        self.assertFalse(result["errors"])
        self.assertFalse(result["trace_errors"])
        self.assertNotIn(("terminate",), self.child.actions)
        self.assertNotIn(("kill",), self.child.actions)

    def test_cleanup_is_idempotent(self):
        first = self.session.cleanup()
        actions, trace = list(self.child.actions), self.trace()
        second = self.session.cleanup()
        self.assertIs(second, first)
        self.assertEqual(len(self.binding.posts), 1)
        self.assertEqual(self.child.actions, actions)
        self.assertEqual(self.trace(), trace)

    def test_foreign_popen_is_rejected_even_with_the_same_pid(self):
        other_handle = FakeChild(self.child.pid)
        for foreign in (self.foreign, other_handle):
            with self.subTest(pid=foreign.pid):
                with self.assertRaises(ValueError):
                    self.session.main_window(foreign)
                with self.assertRaises(ValueError):
                    self.session.cleanup(foreign)
                self.assertEqual(foreign.actions, [])
        self.assertEqual(self.binding.posts, [])
        self.assertEqual(self.binding.hides, [])

    def test_owner_change_during_request_fsync_prevents_post(self):
        def change_owner(row):
            if row["event"] == "wm_quit_send_begin":
                self.binding.owners[101] = (self.foreign.pid, 999)

        with self.on_durable_record(change_owner):
            result = self.session.cleanup()
        self.assertEqual(self.binding.posts, [])
        self.assertEqual(self.binding.hides, [])
        self.assertIsNotNone(result["exit_code"])
        self.assertTrue(any(row["event"] == "wm_quit_send_aborted" for row in self.trace()))

    def test_tid_or_class_change_during_request_fsync_prevents_post(self):
        for change in ("tid", "class"):
            with self.subTest(change=change):
                binding = FakeBinding(FakeChild(12002))
                session = Session(self.folder / change, "hidden", binding=binding)
                session.process = binding.child
                original = session.record

                def record(event, **details):
                    row = original(event, **details)
                    if event == "wm_quit_send_begin":
                        if change == "tid":
                            binding.owners[101] = (binding.child.pid, 999)
                        else:
                            binding.classes[101] = "Unrecognized class"
                    return row

                with patch.object(session, "record", side_effect=record):
                    session.cleanup()
                self.assertEqual(binding.posts, [])

    def test_owner_change_during_hide_fsync_prevents_hide(self):
        self.session.condition = "legacy_rehide"
        self.binding.rows[0]["visible"] = True

        def change_owner(row):
            if row["event"] == "rehide_owned_window":
                self.binding.owners[101] = (self.foreign.pid, 999)

        with self.on_durable_record(change_owner):
            self.session.observe()
        self.assertEqual(self.binding.hides, [])
        self.assertEqual(self.binding.posts, [])
        self.assertTrue(any(row["event"] == "rehide_owned_window_aborted" for row in self.trace()))

    def test_observation_hides_only_owned_recognized_classes(self):
        self.session.condition = "legacy_rehide"
        self.binding.rows[0]["visible"] = True
        self.binding.rows.extend([
            {"hwnd": 102, "pid": self.foreign.pid, "tid": 202, "class": MAIN_WINDOW_CLASS,
             "visible": True, "foreground": True},
            {"hwnd": 103, "pid": self.child.pid, "tid": 203, "class": "Unrecognized class",
             "visible": True, "foreground": False},
            {"hwnd": 104, "pid": self.child.pid, "tid": 204, "class": "ImGui Platform",
             "visible": True, "foreground": False},
        ])
        self.binding.owners.update({102: (self.foreign.pid, 202), 103: (self.child.pid, 203), 104: (self.child.pid, 204)})
        self.binding.classes.update({102: MAIN_WINDOW_CLASS, 103: "Unrecognized class", 104: "ImGui Platform"})
        observed = self.session.observe()
        self.assertEqual(self.binding.hides, [101, 104])
        self.assertTrue(all(row["pid"] == self.child.pid for row in observed))
        self.assertEqual(self.session.main_window(), (101, 201))

    def test_hidden_condition_only_observes_visible_windows(self):
        self.binding.rows[0]["visible"] = True
        self.session.observe()
        self.assertEqual(self.session.visible_samples, 1)
        self.assertEqual(self.binding.hides, [])
        self.assertEqual(self.binding.posts, [])

    def test_refused_post_terminates_only_owned_child(self):
        self.binding.accept_post = False
        result = self.session.cleanup()
        self.assertEqual(len(self.binding.posts), 1)
        self.assertIn(("terminate",), self.child.actions)
        self.assertEqual(result["exit_code"], 77)
        self.assertEqual(result["requests"][0]["end"]["last_error"], 1444)

    def test_accepted_post_with_shutdown_timeout_kills_only_owned_child(self):
        self.binding.exit_on_post = False
        result = self.session.cleanup()
        self.assertEqual(len(self.binding.posts), 1)
        self.assertIn(("kill",), self.child.actions)
        self.assertEqual(result["method"], "kill_after_shutdown_timeout")
        self.assertEqual(result["exit_code"], 88)

    def test_no_owned_main_window_never_posts_to_a_guessed_thread(self):
        self.binding.rows = []
        result = self.session.cleanup()
        self.assertEqual(self.binding.posts, [])
        self.assertIn(("terminate",), self.child.actions)
        self.assertIsNotNone(result["exit_code"])

    def test_shutdown_already_in_native_log_is_waited_without_another_request(self):
        native = self.folder / "logs/application_lifecycle.jsonl"
        native.parent.mkdir(parents=True)
        native.write_text(json.dumps({"event": "wm_quit_received", "pid": self.child.pid}) + "\n", encoding="utf-8")
        self.child.complete_on_wait = 0
        result = self.session.cleanup()
        self.assertEqual(result["method"], "wait_for_already_started_native_shutdown")
        self.assertEqual(self.binding.posts, [])
        self.assertNotIn(("terminate",), self.child.actions)
        self.assertEqual(result["exit_code"], 0)

    def test_already_exited_child_is_not_sent_or_terminated_again(self):
        self.child.returncode = 19
        result = self.session.cleanup()
        self.assertEqual(result["exit_code"], 19)
        self.assertEqual(result["method"], "already_exited")
        self.assertEqual(self.child.actions, [])
        self.assertEqual(self.binding.posts, [])

    def test_request_fsync_failure_never_sends_unconfirmed_quit(self):
        def fail_request(row):
            if row["event"] == "wm_quit_send_begin":
                raise OSError("simulated durable-write failure")

        with self.on_durable_record(fail_request):
            result = self.session.cleanup()
        self.assertEqual(self.binding.posts, [])
        self.assertIn(("terminate",), self.child.actions)
        self.assertTrue(result["trace_errors"])
        self.assertTrue(result["errors"])
        self.assertIsNotNone(result["exit_code"])

    def test_hide_request_fsync_failure_never_hides_unconfirmed_window(self):
        self.session.condition = "legacy_rehide"
        self.binding.rows[0]["visible"] = True

        def fail_request(row):
            if row["event"] == "rehide_owned_window":
                raise OSError("simulated durable-write failure")

        with self.on_durable_record(fail_request), self.assertRaises(OSError):
            self.session.observe()
        self.assertEqual(self.binding.hides, [])
        self.assertEqual(self.binding.posts, [])
        self.assertTrue(self.session.trace_errors)

    def test_launch_failure_can_be_cleaned_up_without_a_child(self):
        self.session.process = None
        with patch.object(process_session.subprocess, "Popen", side_effect=FileNotFoundError("missing fixture executable")) as popen:
            with self.assertRaises(FileNotFoundError):
                self.session.launch(["missing-game.exe"], object())
        popen.assert_called_once()
        result = self.session.cleanup()
        self.assertEqual(result["method"], "not_launched")
        self.assertIsNone(result["exit_code"])
        self.assertEqual(self.binding.posts, [])
        self.assertTrue(any(row["event"] == "launch_failed" for row in self.trace()))

    def test_launch_request_fsync_failure_does_not_start_a_child(self):
        self.session.process = None
        with self.on_durable_record(lambda row: (_ for _ in ()).throw(OSError("disk unavailable"))), patch.object(process_session.subprocess, "Popen") as popen:
            with self.assertRaises(OSError):
                self.session.launch(["unused-game.exe"], object())
        popen.assert_not_called()
        self.assertEqual(self.session.cleanup()["method"], "not_launched")

    def test_launch_completed_record_failure_still_cleans_up_the_created_child(self):
        self.session.process = None

        def fail_completed(row):
            if row["event"] == "launch_completed":
                raise OSError("simulated post-launch record failure")

        with self.on_durable_record(fail_completed), patch.object(process_session.subprocess, "Popen", return_value=self.child):
            with self.assertRaises(OSError):
                self.session.launch(["unused-game.exe"], object())
        self.assertIsNotNone(self.child.returncode)
        self.assertEqual(self.session.cleanup()["exit_code"], 0)
        self.assertEqual(len(self.binding.posts), 1)

    def test_launch_configures_isolated_cwd_and_condition_without_a_native_launch(self):
        for condition in ("hidden", "visible", "legacy_rehide"):
            with self.subTest(condition=condition):
                session = Session(self.folder / condition, condition, binding=self.binding)
                with patch.dict(os.environ, {"DX22_TEST_HIDDEN_WINDOW": "inherited"}), patch.object(process_session.subprocess, "Popen", return_value=self.child) as popen:
                    child = session.launch(["unused-game.exe"], object())
                self.assertIs(child, self.child)
                options = popen.call_args.kwargs
                self.assertEqual(options["cwd"], session.folder)
                self.assertFalse(options["shell"])
                if condition == "hidden":
                    self.assertEqual(options["env"]["DX22_TEST_HIDDEN_WINDOW"], "1")
                else:
                    self.assertNotIn("DX22_TEST_HIDDEN_WINDOW", options["env"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
