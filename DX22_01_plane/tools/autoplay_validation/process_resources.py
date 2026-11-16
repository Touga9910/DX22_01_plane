"""Optional, constant-memory queries through one creation-owned Popen handle.

The caller supplies its own newly created Popen plus the successfully durable
launch pair. Its handle is borrowed, never opened, duplicated or closed here.
Imports are portable; native binding occurs only on the first due sample.
"""
from __future__ import annotations

import ctypes as C
import ntpath
import os
import time


EPOCH_MS = 11644473600000
INTERVAL_SECONDS = 15.0
COUNTERS = ("working_set_bytes", "peak_working_set_bytes", "private_commit_bytes",
            "pagefile_usage_bytes", "page_fault_count", "kernel_handle_count")
DWORD, BOOL, HANDLE = C.c_uint32, C.c_int32, C.c_void_p


class _FileTime(C.Structure):
    _fields_ = [("low", DWORD), ("high", DWORD)]

    def ticks(self):
        return self.low | self.high << 32


class _Memory(C.Structure):
    _fields_ = [("cb", DWORD), ("PageFaultCount", DWORD), *[
        (name, C.c_size_t) for name in (
            "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage",
            "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage",
            "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]]


class _Win32:
    def __init__(self):
        if os.name != "nt" or C.sizeof(C.c_wchar) != 2:
            raise OSError("Resource observation requires Windows")
        self.api = C.WinDLL("kernel32", use_last_error=True)
        for name, args, result in (
            ("GetProcessId", [HANDLE], DWORD),
            ("QueryFullProcessImageNameW", [HANDLE, DWORD, C.c_wchar_p, C.POINTER(DWORD)], BOOL),
            ("GetProcessTimes", [HANDLE, *[C.POINTER(_FileTime)] * 4], BOOL),
            ("GetProcessHandleCount", [HANDLE, C.POINTER(DWORD)], BOOL),
            ("K32GetProcessMemoryInfo", [HANDLE, C.POINTER(_Memory), DWORD], BOOL),
        ):
            function = getattr(self.api, name)
            function.argtypes, function.restype = args, result

    @staticmethod
    def _require(value):
        if not value:
            raise C.WinError(C.get_last_error())
        return value

    def identity(self, handle):
        pid = self._require(self.api.GetProcessId(handle))
        image, length = C.create_unicode_buffer(32768), DWORD(32768)
        self._require(self.api.QueryFullProcessImageNameW(handle, 0, image, C.byref(length)))
        created, exited, kernel, user = (_FileTime() for _ in range(4))
        self._require(self.api.GetProcessTimes(handle, C.byref(created), C.byref(exited), C.byref(kernel), C.byref(user)))
        return {"pid": pid, "exe": image.value, "creation_filetime": created.ticks(),
                "kernel_cpu_100ns": kernel.ticks(), "user_cpu_100ns": user.ticks()}

    def counters(self, handle):
        memory, count = _Memory(), DWORD()
        memory.cb = C.sizeof(memory)
        self._require(self.api.K32GetProcessMemoryInfo(handle, C.byref(memory), C.sizeof(memory)))
        self._require(self.api.GetProcessHandleCount(handle, C.byref(count)))
        return {"working_set_bytes": memory.WorkingSetSize, "peak_working_set_bytes": memory.PeakWorkingSetSize,
                "private_commit_bytes": memory.PrivateUsage, "pagefile_usage_bytes": memory.PagefileUsage,
                "page_fault_count": memory.PageFaultCount, "kernel_handle_count": count.value}


def _image_key(value):
    if value.startswith("\\\\?\\UNC\\"):
        value = "\\\\" + value[8:]
    elif value.startswith("\\\\?\\"):
        value = value[4:]
    if not ntpath.isabs(value) or not ntpath.splitdrive(value)[0]:
        raise ValueError("Launch image must be an absolute Windows path")
    return ntpath.normcase(ntpath.normpath(value))


def _expected(process, begin, end):
    if (begin.get("event") != "launch_requested" or end.get("event") != "launch_completed"
            or begin.get("phase") != "launch" or end.get("phase") != "launch"):
        raise ValueError("Durable launch pair event/phase mismatch")
    pid, low, high, command = end.get("pid"), begin.get("unix_ms"), end.get("unix_ms"), begin.get("command")
    if (type(pid) is not int or pid <= 0 or process.pid != pid
            or type(low) is not int or type(high) is not int or low > high
            or not isinstance(command, list) or not command or not isinstance(command[0], str)):
        raise ValueError("Launch PID/image/window mismatch")
    return {"pid": pid, "image": _image_key(command[0]), "low": low, "high": high}


def _accumulate(item, value):
    if not item:
        item.update(count=0, sum=0, min=value, max=value, first=value, last=value)
    item.update(count=item["count"] + 1, sum=item["sum"] + value,
                min=min(item["min"], value), max=max(item["max"], value), last=value)


class ProcessResources:
    """Call sample() on the controller thread; metric failure disables queries.

    record(event, **fields) must durably stream without retaining sample rows.
    Its failures deliberately propagate into the caller's existing trace error
    handling. A metric failure emits one unavailable event and never ends a game.
    The supplied binding and clocks permit offline tests without any child/API.
    """
    def __init__(self, process, launch_requested, launch_completed, *, record,
                 binding=None, clock=time.monotonic, unix_ms=lambda: time.time_ns() // 1_000_000,
                 query_clock=time.perf_counter):
        self._process, self._record = process, record
        self._binding, self._clock, self._unix_ms = binding, clock, unix_ms
        self._query_clock, self._query_stats = query_clock, {}
        self._next, self._count, self._stats = float("-inf"), 0, {}
        self._status, self._error, self._identity = "ready", None, None
        try:
            self._expected = _expected(process, launch_requested, launch_completed)
            self._handle = int(process._handle)
            if self._handle <= 0:
                raise ValueError("Missing creation-owned Popen handle")
        except Exception as error:
            self._unavailable(error)

    def _unavailable(self, error):
        self._status, self._error = "unavailable", f"{type(error).__name__}: {error}"
        self._record("resources_unavailable", error=self._error)

    def sample(self):
        now = self._clock()
        if self._status in ("unavailable", "process_exited") or now < self._next:
            return
        self._next = now + INTERVAL_SECONDS
        try:
            if self._process.pid != self._expected["pid"] or int(self._process._handle) != self._handle:
                raise ValueError("Popen PID/borrowed handle changed")
            if self._process.poll() is not None:
                self._status = "process_exited"
                return
            if self._binding is None:
                self._binding = _Win32()
            started = self._query_clock()
            identity = self._binding.identity(self._handle)
            query_ms = (self._query_clock() - started) * 1000
            if type(identity["creation_filetime"]) is not int or type(identity["pid"]) is not int:
                raise ValueError("Invalid process identity counter")
            creation_ms = identity["creation_filetime"] // 10000 - EPOCH_MS
            expected = self._expected
            if (identity["pid"] != expected["pid"] or _image_key(identity["exe"]) != expected["image"]
                    or not expected["low"] <= creation_ms <= expected["high"]
                    or self._identity is not None and identity["creation_filetime"] != self._identity["creation_filetime"]):
                raise ValueError("Borrowed handle identity differs from durable launch")
            metrics = {name: identity[name] for name in ("kernel_cpu_100ns", "user_cpu_100ns")}
            started = self._query_clock()
            counters = self._binding.counters(self._handle)
            query_ms += (self._query_clock() - started) * 1000
            if set(counters) != set(COUNTERS):
                raise ValueError("Incomplete or unknown resource counter")
            metrics.update(counters)
            if any(type(value) is not int or value < 0 for value in metrics.values()):
                raise ValueError("Invalid resource counter")
        except Exception as error:
            self._unavailable(error)
            return
        # No recording exception is swallowed or converted into a metric failure.
        self._record("resources_sample", unix_ms=self._unix_ms(), process_id=expected["pid"],
                     creation_filetime=identity["creation_filetime"], metrics=metrics, query_duration_ms=query_ms)
        self._identity, self._status = identity, "observing"
        self._count += 1
        _accumulate(self._query_stats, query_ms)
        for name, value in metrics.items():
            _accumulate(self._stats.setdefault(name, {}), value)

    def summary(self):
        status = self._status
        if status in ("ready", "observing") and getattr(self._process, "returncode", None) is not None:
            status = "process_exited"
        return {"enabled": True, "status": status, "error": self._error,
                "interval_seconds": INTERVAL_SECONDS, "samples": self._count,
                "query_duration_ms": {**self._query_stats, "mean": self._query_stats["sum"] / self._count} if self._count else None,
                "identity": dict(self._identity) if self._identity else None,
                "metrics": {name: {**item, "mean": item["sum"] / item["count"]}
                            for name, item in self._stats.items()}}
