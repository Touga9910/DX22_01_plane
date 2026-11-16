"""Preserve the historical clock/pointer normalization, without hiding choices."""
from __future__ import annotations

from typing import Any

CLOCK_FIELDS = {
    "run_id", "started_at", "ended_at", "recorded_at", "fired_at", "resolved_at",
    "elapsed_from_run_start_ms", "duration_ms", "shot_duration_ms", "measured_at",
    "compiled_date", "compiled_time", "physics_processing_microseconds",
    "average_physics_processing_microseconds_per_step", "lifetime_microseconds",
}
ADDRESS_FIELDS = {"target_id", "source_id", "target_key", "source_key"}


def normalize_run(run: dict[str, Any]) -> Any:
    addresses: dict[str, str] = {}

    def address(value: Any) -> Any:
        if isinstance(value, bool):
            return value
        number = value if isinstance(value, int) else int(value) if isinstance(value, str) and value.isdecimal() else 0
        if number <= 1_000_000:
            return value
        return addresses.setdefault(str(value), f"address_{len(addresses)}")

    def walk(value: Any, parent_key: str = "") -> Any:
        if isinstance(value, dict):
            result = {}
            for key, item in value.items():
                normalized_key = address(key) if parent_key.endswith("_by_target") else key
                if key in CLOCK_FIELDS:
                    result[normalized_key] = {"normalized_clock_type": type(item).__name__}
                elif key in ADDRESS_FIELDS:
                    result[normalized_key] = address(item)
                else:
                    result[normalized_key] = walk(item, key)
            return result
        if isinstance(value, list):
            return [walk(item, parent_key) for item in value]
        return value

    return walk(run)
