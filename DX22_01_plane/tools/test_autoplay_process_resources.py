"""Check resource-observer ownership and failure boundaries without native IO.

Fake children/bindings exercise the public modules directly. Temporary JSONL
files verify durable recording; no native process, game or desktop is used.
"""
import gc
import json
from pathlib import Path
import tempfile
import types
import unittest
import weakref
from unittest.mock import patch

from autoplay_validation import process_resources as resources, process_session as session_module
from autoplay_validation.lifecycle import classify_result
from test_autoplay_lifecycle import completed_session


class Child:
    pid, _handle, returncode = 42, 1234, None

    def poll(self):
        return self.returncode


class Binding:
    def __init__(self):
        self.calls, self.counter_calls, self.value = [], 0, 10
        self.data = {"pid": 42, "exe": r"C:\GAME\game.exe",
                     "creation_filetime": (resources.EPOCH_MS + 1001) * 10000,
                     "kernel_cpu_100ns": 2, "user_cpu_100ns": 3}

    def identity(self, handle):
        self.calls.append(handle)
        return dict(self.data)

    def counters(self, handle):
        self.counter_calls += 1
        return {key: self.value for key in resources.COUNTERS}


class ResourceTests(unittest.TestCase):
    def setUp(self):
        self.child, self.binding, self.rows, self.now = Child(), Binding(), [], [0.0]
        self.begin = {"event": "launch_requested", "phase": "launch", "unix_ms": 1000,
                      "command": [r"C:\game\game.exe"]}
        self.end = {"event": "launch_completed", "phase": "launch", "unix_ms": 1002, "pid": 42}

    def observer(self, record=None):
        return resources.ProcessResources(self.child, self.begin, self.end,
            binding=self.binding, clock=lambda: self.now[0], unix_ms=lambda: 1001,
            record=record or (lambda event, **fields: self.rows.append({"event": event, **fields})))

    def test_fifteen_second_interval_and_scalar_aggregation(self):
        observer = self.observer()
        observer.sample()
        self.now[0] = 14.999
        observer.sample()
        self.assertEqual(self.binding.counter_calls, 1)
        self.binding.value, self.now[0] = 30, 15
        observer.sample()
        summary = observer.summary()
        self.assertEqual(summary["samples"], 2)
        self.assertEqual(summary["metrics"]["working_set_bytes"],
            {"count": 2, "sum": 40, "min": 10, "max": 30, "first": 10, "last": 30, "mean": 20})
        self.assertNotIn("rows", observer.__dict__)
        self.assertEqual(len(observer._stats), 8)

    def test_many_samples_keep_only_fixed_metric_set(self):
        observer = self.observer(record=lambda *_args, **_fields: None)
        for index in range(1000):
            self.now[0] = index * 15
            observer.sample()
        self.assertEqual(observer.summary()["samples"], 1000)
        self.assertEqual(len(observer._stats), 8)

    def test_identity_mismatch_never_queries_memory(self):
        for change in ({"pid": 43}, {"exe": r"C:\other.exe"},
                       {"creation_filetime": (resources.EPOCH_MS + 2000) * 10000},
                       {"creation_filetime": (resources.EPOCH_MS + 999) * 10000}):
            with self.subTest(change=change):
                self.binding = Binding()
                self.binding.data.update(change)
                self.rows.clear()
                observer = self.observer()
                observer.sample()
                self.now[0] += 30
                observer.sample()
                self.assertEqual(observer.summary()["status"], "unavailable")
                self.assertEqual(self.binding.counter_calls, 0)
                self.assertEqual([r["event"] for r in self.rows], ["resources_unavailable"])

    def test_launch_pair_rejection_is_offline(self):
        for field, value in (("phase", "explicit_injection"), ("pid", 43), ("unix_ms", 999)):
            with self.subTest(field=field):
                self.end[field] = value
                observer = self.observer()
                observer.sample()
                self.assertEqual(observer.summary()["status"], "unavailable")
                self.assertEqual(self.binding.calls, [])
                self.end = {"event": "launch_completed", "phase": "launch", "unix_ms": 1002, "pid": 42}

    def test_end_millisecond_fraction_is_allowed(self):
        self.binding.data["creation_filetime"] = (resources.EPOCH_MS + 1002) * 10000 + 9999
        observer = self.observer()
        observer.sample()
        self.assertEqual(observer.summary()["samples"], 1)

    def test_later_handle_or_creation_change_is_rejected(self):
        observer = self.observer()
        observer.sample()
        self.binding.data["creation_filetime"] += 1
        self.now[0] = 15
        observer.sample()
        self.assertEqual(self.binding.counter_calls, 1)
        self.assertEqual(observer.summary()["status"], "unavailable")
        self.child._handle = 999
        self.rows.clear()
        observer = self.observer()
        self.child._handle = 998
        observer.sample()
        self.assertEqual(self.binding.counter_calls, 1)

    def test_exit_does_not_query_or_close_handle(self):
        self.child.returncode = 0
        observer = self.observer()
        observer.sample()
        self.assertEqual(observer.summary()["status"], "process_exited")
        self.assertEqual(self.binding.calls, [])
        self.assertEqual(self.child._handle, 1234)

    def test_metric_failure_is_once_and_does_not_raise(self):
        self.binding.counters = lambda _handle: (_ for _ in ()).throw(OSError(5, "denied"))
        observer = self.observer()
        observer.sample()
        self.now[0] = 30
        observer.sample()
        self.assertEqual(observer.summary()["status"], "unavailable")
        self.assertEqual(len(self.rows), 1)
        self.assertIsNone(self.child.returncode)

    def test_missing_or_invalid_metric_disables_observation(self):
        for values in ({}, {key: float("nan") for key in resources.COUNTERS},
                       {key: -1 for key in resources.COUNTERS}):
            with self.subTest(values=values):
                self.binding.counters = lambda _handle: values
                observer = self.observer()
                observer.sample()
                self.assertEqual(observer.summary()["status"], "unavailable")

    def test_record_failure_is_not_disguised_as_metric_failure(self):
        def fail(*_args, **_fields):
            raise OSError("durable record failed")
        observer = self.observer(record=fail)
        with self.assertRaisesRegex(OSError, "durable record"):
            observer.sample()
        self.assertEqual(observer.summary()["samples"], 0)

    def test_unavailable_record_failure_also_propagates(self):
        self.end["pid"] = 43
        with self.assertRaises(OSError):
            self.observer(record=lambda *_args, **_fields: (_ for _ in ()).throw(OSError("disk failed")))

    def test_structure_layout_is_native_width(self):
        self.assertEqual(resources.C.sizeof(resources._FileTime), 8)
        self.assertEqual(resources.C.sizeof(resources._Memory), 80 if resources.C.sizeof(resources.HANDLE) == 8 else 44)

    def test_summary_uses_cached_exit_without_native_query(self):
        observer = self.observer()
        observer.sample()
        self.child.returncode = 0
        self.assertEqual(observer.summary()["status"], "process_exited")
        self.assertEqual(self.binding.counter_calls, 1)
        self.binding.counters = lambda _handle: (_ for _ in ()).throw(OSError("unavailable"))
        self.child.returncode, self.now[0] = None, 15
        observer.sample()
        self.child.returncode = 0
        self.assertEqual(observer.summary()["status"], "unavailable")

    def test_query_duration_excludes_record_callback(self):
        ticks = iter((1.0, 1.002, 1.004, 1.007))
        observer = resources.ProcessResources(self.child, self.begin, self.end,
            record=lambda event, **fields: self.rows.append(fields), binding=self.binding,
            query_clock=lambda: next(ticks))
        observer.sample()
        self.assertAlmostEqual(self.rows[0]["query_duration_ms"], 5)
        self.assertAlmostEqual(observer.summary()["query_duration_ms"]["mean"], 5)


class IntegrationTests(unittest.TestCase):
    def session(self, folder):
        binding = types.SimpleNamespace(tick=lambda: 100)
        return session_module.Session(Path(folder), "hidden", binding=binding)

    def test_unretained_sample_still_durable_and_quit_still_retained(self):
        with tempfile.TemporaryDirectory() as folder:
            session = self.session(folder)
            session.record("resources_sample", _retain=False, metrics={"working_set_bytes": 10})
            quit_row = session.record("wm_quit_send_begin", phase="fixture_finally", tid=1, wparam=2, lparam=3)
            rows = [json.loads(line) for line in (Path(folder) / "request_events.jsonl").read_text().splitlines()]
            self.assertEqual([r["event"] for r in rows], ["resources_sample", "wm_quit_send_begin"])
            self.assertEqual(session.events, [quit_row])
            self.assertTrue(all("_retain" not in row for row in rows))

    def test_unretained_fsync_failure_is_trace_error_and_propagates(self):
        with tempfile.TemporaryDirectory() as folder:
            session = self.session(folder)
            with patch.object(session_module.os, "fsync", side_effect=OSError("fsync failed")):
                with self.assertRaises(OSError):
                    session.record("resources_sample", _retain=False)
            self.assertEqual(session.events, [])
            self.assertIn("resources_sample", session.trace_errors[0])
            data = completed_session()
            data["trace_errors"] = session.trace_errors
            self.assertEqual(classify_result(data)["outcome"], "diagnostics_failure")

    def test_disk_resource_rows_do_not_change_request_matching(self):
        data = completed_session()
        expected = classify_result(data)
        data["events"].insert(1, {"event": "resources_sample", "phase": "observation", "metrics": {}})
        data["events"].insert(3, {"event": "resources_unavailable", "phase": "observation", "error": "denied"})
        self.assertEqual(classify_result(data), expected)

    def test_observation_is_disabled_by_default(self):
        with tempfile.TemporaryDirectory() as folder:
            session = self.session(folder)
            self.assertEqual(session.resources_summary(), {"enabled": False, "status": "disabled", "samples": 0})
            self.assertFalse(session._observe_resources)

    def test_enabled_session_attaches_only_after_durable_launch_pair(self):
        with tempfile.TemporaryDirectory() as folder:
            session = session_module.Session(Path(folder), "hidden", observe_resources=True,
                binding=types.SimpleNamespace(tick=lambda: 100))
            child, observed = Child(), []

            def attach(process, begin, end, *, record):
                self.assertIs(process, child)
                disk = [json.loads(line) for line in (Path(folder) / "request_events.jsonl").read_text().splitlines()]
                self.assertEqual(disk, [begin, end])
                self.assertEqual(begin["event"], "launch_requested")
                self.assertEqual(end["event"], "launch_completed")
                record("resources_sample", metrics={"working_set_bytes": 10})
                observed.append(True)
                return types.SimpleNamespace(summary=lambda: {"enabled": True, "samples": 1})

            with patch.object(session_module.subprocess, "Popen", return_value=child), \
                    patch.object(session_module, "ProcessResources", side_effect=attach):
                session.launch([r"C:\game\game.exe"], None)
            self.assertEqual(observed, [True])
            self.assertEqual([r["event"] for r in session.events], ["launch_requested", "launch_completed"])
            self.assertEqual(session.resources_summary()["samples"], 1)

    def test_real_observer_callback_failure_reaches_session_trace_errors(self):
        with tempfile.TemporaryDirectory() as folder:
            session = self.session(folder)
            child, binding = Child(), Binding()
            begin = {"event": "launch_requested", "phase": "launch", "unix_ms": 1000,
                     "command": [r"C:\game\game.exe"]}
            end = {"event": "launch_completed", "phase": "launch", "unix_ms": 1002, "pid": 42}
            session._resources = resources.ProcessResources(child, begin, end, binding=binding,
                record=lambda event, **fields: session.record(event, _retain=False, **fields))
            with patch.object(session_module.os, "fsync", side_effect=OSError("disk failed")):
                with self.assertRaises(OSError):
                    session.observe()
            self.assertIn("resources_sample", session.trace_errors[0])
            self.assertEqual(session._resources.summary()["samples"], 0)

    def test_observer_does_not_keep_session_alive_without_cyclic_gc(self):
        with tempfile.TemporaryDirectory() as folder:
            session = session_module.Session(Path(folder), "hidden", observe_resources=True,
                binding=types.SimpleNamespace(tick=lambda: 100))
            with patch.object(session_module.subprocess, "Popen", return_value=Child()):
                session.launch([r"C:\game\game.exe"], None)
            observer, session_ref = session._resources, weakref.ref(session)
            previously_enabled = gc.isenabled()
            gc.disable()
            try:
                del session
                self.assertIsNone(session_ref(), "Borrowed observer callback must not retain Session")
                with self.assertRaises(ReferenceError):
                    observer._record("resources_sample", metrics={})
            finally:
                if previously_enabled:
                    gc.enable()


if __name__ == "__main__":
    unittest.main()
