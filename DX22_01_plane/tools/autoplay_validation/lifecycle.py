"""Read native lifecycle evidence and classify a completed, isolated trial.

This module has no Win32 dependency. A cleanup token establishes correspondence
with a recorded request; it does not authenticate the sender of an OS message.
"""
from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping


SHUTDOWN_EVENTS = frozenset({
    "wm_quit_received", "shutdown_begin", "shutdown_completed", "application_completed",
})
CLOSE_EVENTS = frozenset({
    "sc_close_received", "wm_close_received", "wm_close_result", "wm_destroy_received",
    "wm_queryendsession_received", "wm_endsession_received",
})


class NativeLogError(ValueError):
    """A complete native log record was malformed or was not an object."""


def read_native(folder: Path, *, final: bool = False) -> list[dict[str, Any]]:
    """Read ``folder/logs/application_lifecycle.jsonl`` (or a JSONL path).

    While the child is alive, only a malformed *unterminated final line* may be
    an append in progress. All complete lines are checked. After waiting for the
    child, callers must use ``final=True`` so a truncated tail cannot disappear.
    A missing log is represented by an empty list, never by fabricated events.
    """
    path = Path(folder)
    if path.suffix.lower() != ".jsonl":
        path = path / "logs" / "application_lifecycle.jsonl"
    if not path.exists():
        return []
    content = path.read_text(encoding="utf-8-sig")
    lines = content.splitlines(keepends=True)
    rows: list[dict[str, Any]] = []
    for index, line in enumerate(lines):
        if not line.strip():
            continue
        try:
            row = json.loads(line)
        except ValueError as error:
            incomplete_tail = index == len(lines) - 1 and not line.endswith(("\n", "\r"))
            if not final and incomplete_tail:
                break
            raise NativeLogError(f"{path}:{index + 1}: {error}") from error
        if not isinstance(row, dict):
            raise NativeLogError(f"{path}:{index + 1}: lifecycle record must be an object")
        rows.append(row)
    return rows


def _integer(value: Any) -> int | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError:
            return None
    return None


def _close_evidence(native: list[dict[str, Any]]) -> tuple[list, list]:
    """Keep cancelled close dialogs as evidence without calling them exits."""
    close_rows = [row for row in native if row.get("event") in CLOSE_EVENTS]
    cancelled: list[dict[str, Any]] = []
    pending: list[dict[str, Any]] = []
    active: list[dict[str, Any]] = []
    for row in close_rows:
        event = row.get("event")
        if event in ("sc_close_received", "wm_close_received"):
            pending.append(row)
        elif event == "wm_close_result":
            if _integer(row.get("result")) == 2:  # IDCANCEL
                cancelled.extend(pending + [row])
            else:
                active.extend(pending + [row])
            pending = []
        elif event == "wm_destroy_received" or (
                event == "wm_endsession_received" and _integer(row.get("wparam")) not in (None, 0)):
            active.append(row)
    active.extend(pending)
    return active, cancelled


@dataclass(frozen=True)
class _Trial:
    pid: int | None
    native: list[dict[str, Any]]
    requests: list[dict[str, Any]]
    cleanup: Mapping[str, Any]
    error: str
    exit_code: Any
    gameplay_ok: bool
    failure_kind: Any
    timeout_ms: int
    expected_nonce: Mapping[str, Any]
    condition: Any
    visible_samples: Any
    native_parse_error: Any
    trace_errors: list
    reference_equal: Any


def _normalize_input(data: Mapping[str, Any]) -> _Trial:
    """Resolve public aliases once, before interpreting any lifecycle evidence."""
    manifest = data.get("manifest") or {}
    cleanup = data.get("cleanup") or {}
    timeout_ms = _integer(data.get("cleanup_timeout_ms", 10000))
    return _Trial(
        pid=_integer(data.get("process_id")),
        native=list(data.get("native", data.get("native_events", [])) or []),
        requests=list(data.get("events", data.get("request_events", [])) or []),
        cleanup=cleanup,
        error=str(data.get("error", manifest.get("error")) or ""),
        exit_code=data.get("exit_code", cleanup.get("exit_code")),
        gameplay_ok=data.get("gameplay_ok", manifest.get("gameplay_ok", manifest.get("ok"))) is True,
        failure_kind=manifest.get("failure_kind"),
        timeout_ms=max(0, timeout_ms if timeout_ms is not None else 10000),
        expected_nonce=data.get("nonce", data.get("cleanup_nonce")) or {},
        condition=data.get("condition"),
        visible_samples=data.get("visible_samples", data.get("visible_window_samples")),
        native_parse_error=data.get("native_parse_error"),
        trace_errors=list(data.get("trace_errors", cleanup.get("trace_errors")) or []),
        reference_equal=data.get("reference_comparable_equal"),
    )


@dataclass(frozen=True)
class _CleanupMatch:
    quits: list
    starts: list
    ends: list
    accepted: list
    matches: list
    unmatched: list

    @property
    def matched(self) -> bool:
        return len(self.quits) == len(self.starts) == len(self.ends) == len(self.accepted) == len(self.matches) == 1


def _request_corresponds(trial: _Trial, received: Mapping, begin: Mapping, end: Mapping) -> bool:
    """Match ownership, token and boot-clock bounds independently of log writes."""
    cleanup_phase = begin.get("phase") == end.get("phase") == "fixture_finally"
    fields_match = all(
        _integer(begin.get(key)) is not None
        and _integer(begin.get(key)) == _integer(end.get(key)) == _integer(received.get(key))
        for key in ("pid", "tid", "wparam", "lparam"))
    expected_match = all(
        key in trial.expected_nonce and _integer(trial.expected_nonce[key]) is not None
        and _integer(begin.get(key)) == _integer(trial.expected_nonce[key])
        for key in ("wparam", "lparam"))
    begin_tick = _integer(begin.get("uptime_ms"))
    receive_tick = _integer(received.get("uptime_ms"))
    end_tick = _integer(end.get("uptime_ms"))
    time_match = (None not in (begin_tick, receive_tick, end_tick)
        and begin_tick <= end_tick
        and begin_tick <= receive_tick <= end_tick + trial.timeout_ms)
    return cleanup_phase and fields_match and expected_match and time_match and _integer(begin.get("pid")) == trial.pid


def _match_cleanup_requests(trial: _Trial) -> _CleanupMatch:
    quits = [row for row in trial.native if row.get("event") == "wm_quit_received"]
    starts = [row for row in trial.requests if row.get("event") == "wm_quit_send_begin"]
    ends = [row for row in trial.requests if row.get("event") == "wm_quit_send_end"]
    accepted = [row for row in ends if row.get("ok") is True and row.get("last_error") == 0]
    matches = []
    unmatched = []
    for quit_row in quits:
        matching_request = next((
            {"begin": begin, "end": end, "native": quit_row}
            for begin in starts for end in accepted
            if _request_corresponds(trial, quit_row, begin, end)), None)
        if matching_request:
            matches.append(matching_request)
        else:
            unmatched.append(quit_row)
    return _CleanupMatch(quits, starts, ends, accepted, matches, unmatched)


@dataclass(frozen=True)
class _ShutdownEvidence:
    prior: list
    earlier: list
    active_closes: list
    cancelled_closes: list
    close_rows: list
    forced: list
    already_exited: bool
    unexpected_error: bool
    native_missing_exit: bool
    unrequested_shutdown: bool

    def unexpected(self, match: _CleanupMatch) -> bool:
        return bool(self.prior or self.earlier or self.already_exited or match.unmatched
                    or self.unexpected_error or self.active_closes or self.native_missing_exit
                    or self.unrequested_shutdown)


def _collect_shutdown_evidence(trial: _Trial, match: _CleanupMatch) -> _ShutdownEvidence:
    """Preserve competing exits even if a later cleanup request was accepted."""
    entries = [row for row in trial.requests if row.get("event") == "cleanup_entered"]
    entry = entries[0] if entries else None
    prior = list(entry.get("prior_native_shutdown", [])) if entry else []
    for row in trial.requests:
        if row.get("event") == "cleanup_observed_competing_shutdown":
            prior.extend(row.get("native") or [])
    first_send = min((_integer(row.get("uptime_ms")) for row in match.starts
                      if _integer(row.get("uptime_ms")) is not None), default=None)
    earlier = [row for row in trial.native if row.get("event") in SHUTDOWN_EVENTS
               and first_send is not None and _integer(row.get("uptime_ms")) is not None
               and _integer(row.get("uptime_ms")) < first_send]
    active_closes, cancelled_closes = _close_evidence(trial.native)
    forced = [row for row in trial.requests if row.get("event") in ("terminate_owned_child", "kill_owned_child")]
    has_shutdown = any(row.get("event") in SHUTDOWN_EVENTS for row in trial.native)
    return _ShutdownEvidence(
        prior=prior, earlier=earlier, active_closes=active_closes, cancelled_closes=cancelled_closes,
        close_rows=[row for row in trial.native if row.get("event") in CLOSE_EVENTS], forced=forced,
        already_exited=entry is not None and entry.get("poll") is not None,
        unexpected_error="Fixture process exited unexpectedly" in trial.error,
        native_missing_exit=trial.exit_code is not None and not match.starts and not forced and not has_shutdown,
        unrequested_shutdown=bool(not match.starts and not forced and has_shutdown),
    )


def _shutdown_completed_in_order(native: list, quit_row: Mapping) -> bool:
    """A matching quit is insufficient: every orderly-shutdown marker must follow."""
    cursor = next(index for index, row in enumerate(native) if row is quit_row)
    previous_tick = _integer(quit_row.get("uptime_ms"))
    previous_sequence = _integer(quit_row.get("sequence"))
    for name in ("shutdown_begin", "shutdown_completed", "application_completed"):
        indices = [index for index, row in enumerate(native) if row.get("event") == name]
        if len(indices) != 1 or indices[0] <= cursor:
            return False
        cursor = indices[0]
        marker_tick = _integer(native[cursor].get("uptime_ms"))
        marker_sequence = _integer(native[cursor].get("sequence"))
        if (marker_tick is None or previous_tick is None or marker_tick < previous_tick
                or (marker_sequence is not None and previous_sequence is not None
                    and marker_sequence <= previous_sequence)):
            return False
        previous_tick, previous_sequence = marker_tick, marker_sequence
    return True


def _diagnostic_checks(trial: _Trial, match: _CleanupMatch, shutdown: _ShutdownEvidence) -> dict[str, bool]:
    same_pid = bool(trial.native) and trial.pid is not None and all(
        _integer(row.get("pid")) == trial.pid for row in trial.native)
    hidden_ok = trial.condition != "hidden" or (
        trial.visible_samples == 0 and bool(trial.native)
        and all(row.get("hidden_window") is True and row.get("visible") is False for row in trial.native))
    return {
        "native_log_valid": not trial.native_parse_error,
        "native_same_pid": same_pid,
        "own_tagged_quit": match.matched,
        "no_prior_shutdown": not (shutdown.prior or shutdown.earlier or shutdown.already_exited or shutdown.unexpected_error),
        "no_unsolicited_close": not shutdown.active_closes,
        "zero_exit": trial.exit_code == 0,
        "shutdown_order": match.matched and _shutdown_completed_in_order(trial.native, match.quits[0]),
        "hidden_window_condition": hidden_ok,
        "no_forced_cleanup": not shutdown.forced,
        "durable_request_trace": not trial.trace_errors,
        "cleanup_error_free": not trial.cleanup.get("errors"),
    }


def _choose_outcome(trial: _Trial, match: _CleanupMatch, shutdown: _ShutdownEvidence,
                    checks: Mapping[str, bool], diagnostics_ok: bool) -> tuple[str, list[str]]:
    """Keep exit attribution ahead of fixture failures and comparison results."""
    reasons: list[str] = []
    if trial.pid is None:
        outcome = "launch_failure"
        reasons.append("process_not_launched")
    elif shutdown.unexpected(match):
        outcome = "unexpected_close_request" if shutdown.active_closes else "unexpected_exit"
        if shutdown.active_closes:
            reasons.append("unsolicited_close_request")
        if shutdown.prior or shutdown.earlier:
            reasons.append("native_shutdown_before_cleanup")
        if shutdown.already_exited or shutdown.unexpected_error:
            reasons.append("process_exited_before_cleanup")
        if match.unmatched:
            reasons.append("native_quit_not_attributable_to_cleanup")
        if shutdown.native_missing_exit:
            reasons.append("process_exited_without_native_shutdown")
        if shutdown.unrequested_shutdown and not match.unmatched:
            reasons.append("native_shutdown_without_cleanup_request")
    elif shutdown.forced or len(match.accepted) < len(match.ends):
        outcome = "cleanup_failure"
        reasons.append("forced_cleanup" if shutdown.forced else "quit_post_failed")
    elif match.matched and (trial.exit_code != 0 or not checks["shutdown_order"]):
        outcome = "cleanup_failure"
        reasons.append("nonzero_exit_after_cleanup" if trial.exit_code != 0 else "native_shutdown_incomplete")
    elif trial.exit_code is None:
        outcome = "cleanup_failure"
        reasons.append("process_still_running")
    elif not trial.gameplay_ok:
        outcome = "fixture_timeout" if "TimeoutError" in trial.error or trial.failure_kind == "fixture_timeout" else "fixture_failure"
        reasons.append("fixture_deadline_exceeded" if outcome == "fixture_timeout" else "fixture_assertion_failed")
    elif not diagnostics_ok:
        outcome = "diagnostics_failure"
        reasons.extend(name for name, passed in checks.items() if not passed)
    elif trial.reference_equal is False:
        outcome = "gameplay_difference"
        reasons.append("reference_comparable_mismatch")
    else:
        outcome = "ordinary_success"
        reasons.append("tagged_cleanup_and_orderly_shutdown")
    return outcome, reasons


def classify_result(data: Mapping[str, Any]) -> dict[str, Any]:
    """Classify captured evidence without reading files or operating processes.

    The public input keys are ``process_id``, ``gameplay_ok``, ``error``,
    ``cleanup`` (including final ``exit_code``), ``native_events``,
    ``request_events``, ``condition``, ``visible_samples`` and ``nonce``.
    ``manifest`` (``ok``/``error``), ``native``, ``events``,
    ``visible_window_samples``, ``cleanup_nonce`` and top-level ``exit_code``
    are accepted aliases for preserved capture records.
    Request rows use the Session trace schema; native rows retain C++ JSON fields.
    ``native_parse_error`` and ``reference_comparable_equal`` are optional.
    """
    trial = _normalize_input(data)
    match = _match_cleanup_requests(trial)
    shutdown = _collect_shutdown_evidence(trial, match)
    checks = _diagnostic_checks(trial, match, shutdown)
    unexpected_shutdown = shutdown.unexpected(match)
    diagnostics_ok = all(checks.values()) and not unexpected_shutdown
    outcome, reasons = _choose_outcome(trial, match, shutdown, checks, diagnostics_ok)
    return {
        "outcome": outcome, "reason": reasons[0], "reasons": reasons,
        "gameplay_ok": trial.gameplay_ok, "diagnostics_ok": diagnostics_ok,
        "cleanup_matched": match.matched, "unexpected_shutdown": unexpected_shutdown,
        "checks": checks,
        "evidence": {"native_quit": match.quits, "unmatched_quit": match.unmatched,
                     "matched_requests": match.matches, "prior_native_shutdown": shutdown.prior,
                     "shutdown_before_send": shutdown.earlier, "close_events": shutdown.close_rows,
                     "active_close_events": shutdown.active_closes, "cancelled_close_events": shutdown.cancelled_closes,
                     "forced_cleanup_requests": shutdown.forced, "native_parse_error": trial.native_parse_error,
                     "trace_errors": trial.trace_errors, "cleanup_errors": list(trial.cleanup.get("errors") or [])},
    }
