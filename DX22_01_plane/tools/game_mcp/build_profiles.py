"""MCPサーバーとラン収集器で共有するビルド方針プロフィール。"""

from __future__ import annotations

import copy
import hashlib
import json
import threading
from pathlib import Path
from typing import Any

from bridge_store import GameBridgeError


VALID_BUILD_PROFILES = (
    "standard",
    "heavy",
    "pierce",
    "bounce",
    "anchor",
)
VALID_BALL_IDS = {
    "player_standard",
    "player_heavy",
    "player_pierce",
    "player_bounce",
    "player_anchor",
}
VALID_REWARD_PRIORITIES = {
    "money",
    "new_ball",
    "ball_upgrade",
    "hp_recovery",
    "relic",
}


def profile_settings_hash(profile: dict[str, Any]) -> str:
    payload = json.dumps(
        profile,
        ensure_ascii=True,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def load_build_profiles(
    path: Path,
) -> tuple[dict[str, dict[str, Any]], str]:
    try:
        with path.open("r", encoding="utf-8") as source:
            document = json.load(source)
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(
            f"ビルド方針設定を読み込めません: {path}"
        ) from error

    profiles_document = document.get("profiles")
    if not isinstance(profiles_document, dict):
        raise RuntimeError(
            "ビルド方針設定にprofilesオブジェクトが必要です。"
        )
    default_profile = str(document.get("default_profile", "standard"))

    profiles: dict[str, dict[str, Any]] = {}
    for profile_id in VALID_BUILD_PROFILES:
        profile = profiles_document.get(profile_id)
        if not isinstance(profile, dict):
            raise RuntimeError(
                f"ビルド方針設定に{profile_id}がありません。"
            )
        for key in (
            "new_ball_priority",
            "upgrade_priority",
            "reward_priority",
            "ball_score_weights",
            "relic_policy",
            "shot_policy",
            "instructions",
        ):
            if key not in profile:
                raise RuntimeError(
                    f"{profile_id}.{key}が必要です。"
                )
        for key in ("new_ball_priority", "upgrade_priority"):
            values = profile[key]
            if (
                not isinstance(values, list)
                or not values
                or not all(value in VALID_BALL_IDS for value in values)
            ):
                raise RuntimeError(
                    f"{profile_id}.{key}に有効なボールIDが必要です。"
                )
        rewards = profile["reward_priority"]
        if (
            not isinstance(rewards, list)
            or set(rewards) != VALID_REWARD_PRIORITIES
        ):
            raise RuntimeError(
                f"{profile_id}.reward_priorityには5報酬を"
                "重複なく指定してください。"
            )
        stage_choice_policy = profile.get("stage_choice_policy", {})
        need_priority = (
            stage_choice_policy.get("need_priority")
            if isinstance(stage_choice_policy, dict)
            else None
        )
        if (
            not isinstance(need_priority, list)
            or set(need_priority) != VALID_REWARD_PRIORITIES
            or len(need_priority) != len(VALID_REWARD_PRIORITIES)
        ):
            raise RuntimeError(
                f"{profile_id}.stage_choice_policy.need_priorityには"
                "5報酬を重複なく指定してください。"
            )
        instructions = profile["instructions"]
        if (
            not isinstance(instructions, list)
            or not instructions
            or not all(
                isinstance(instruction, str) and instruction
                for instruction in instructions
            )
        ):
            raise RuntimeError(
                f"{profile_id}.instructionsは空でない文字列配列に"
                "してください。"
            )
        profiles[profile_id] = copy.deepcopy(profile)

    if default_profile not in profiles:
        raise RuntimeError(
            "default_profileは定義済みビルド方針を指定してください。"
        )
    return profiles, default_profile


class BuildProfileController:
    def __init__(
        self,
        profiles: dict[str, dict[str, Any]],
        initial_profile: str,
    ) -> None:
        self._profiles = profiles
        self._lock = threading.Lock()
        self._profile_id = ""
        self.set_profile(initial_profile)

    @property
    def profile_id(self) -> str:
        with self._lock:
            return self._profile_id

    def set_profile(self, profile_id: str) -> dict[str, Any]:
        if profile_id not in self._profiles:
            raise GameBridgeError(
                "build_profileはstandard、heavy、pierce、bounce、"
                "anchorのいずれかを指定してください。"
            )
        with self._lock:
            self._profile_id = profile_id
            return self._snapshot_unlocked()

    def snapshot(self) -> dict[str, Any]:
        with self._lock:
            return self._snapshot_unlocked()

    def _snapshot_unlocked(self) -> dict[str, Any]:
        profile = copy.deepcopy(self._profiles[self._profile_id])
        return {
            "id": self._profile_id,
            "settings_hash": profile_settings_hash(profile),
            **profile,
        }


def compose_control_profile(
    player_profile: dict[str, Any],
    build_profile: dict[str, Any],
) -> dict[str, Any]:
    result = copy.deepcopy(player_profile)
    for key in (
        "pocket_tactics",
        "stage_choice_policy",
        "relic_policy",
    ):
        override = build_profile.get(key)
        if not isinstance(override, dict):
            continue
        base = result.get(key, {})
        if not isinstance(base, dict):
            base = {}
        result[key] = {**base, **copy.deepcopy(override)}

    player_instructions = result.get("instructions", [])
    build_instructions = build_profile.get("instructions", [])
    result["instructions"] = [
        *(
            player_instructions
            if isinstance(player_instructions, list)
            else []
        ),
        *(
            build_instructions
            if isinstance(build_instructions, list)
            else []
        ),
    ]
    result["build_profile"] = {
        "id": build_profile["id"],
        "label": build_profile.get("label", build_profile["id"]),
        "settings_hash": build_profile["settings_hash"],
    }
    result["build_policy"] = copy.deepcopy(build_profile)
    return result
