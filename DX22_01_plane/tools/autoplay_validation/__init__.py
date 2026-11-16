"""Reusable real-game autoplay validation and lifecycle evidence helpers."""

from .lifecycle import NativeLogError, classify_result, read_native
from .process_session import Session

__all__ = ["NativeLogError", "Session", "classify_result", "read_native"]
