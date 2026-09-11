from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def source(name: str) -> str:
    return (ROOT / name).read_text(encoding="utf-8-sig")


class CppAutoPlayRecoveryContractTests(unittest.TestCase):
    def test_clear_invariant_recovers_outside_ball_movement(self) -> None:
        battle = source("BattleController.cpp")
        header = source("BattleController.h")
        bridge = source("GameBattleBridge.cpp")

        self.assertIn('TryRecoverClearedBattle("state_invariant")', battle)
        self.assertIn('"battle_clear_state_recovered"', bridge)
        self.assertIn("!AreAllEnemiesDefeated()", battle)
        self.assertIn("bool TryRecoverClearedBattle(const char* source);", header)

    def test_collision_damage_is_limited_to_active_shot(self) -> None:
        collision = source("BallCollisionComponent.cpp")
        gate = collision.index("BattleState::BallsMoving")
        damage = collision.index("myEnemy->TakeDamage", gate)

        self.assertLess(gate, damage)
        self.assertIn("return;", collision[gate:damage])

    def test_stop_requires_stable_frames_and_clears_motion(self) -> None:
        battle = source("BattleController.cpp")
        player = source("PlayerBall.cpp")

        self.assertIn("kRequiredStoppedTicks = 11", source("BattleController.h"))
        self.assertIn("m_AllBallsStoppedTickCount >= kRequiredStoppedTicks", battle)
        self.assertIn("physics->Velocity() =", battle)
        self.assertIn("physics->Acceleration() =", battle)
        self.assertIn("BallPhysicsRules::PlayerFriction", player)
        self.assertIn("velocity = acceleration = Vector3::Zero", source("BallPhysicsRules.h"))

    def test_autoplay_scores_kills_and_pocket_risk(self) -> None:
        autoplay = source("GameAutoPlay.cpp")

        for contract in (
            "isKillable ? 180.0f",
            "pocketRisk",
            "safeMaximumPower",
            '"autoplay_waiting"',
            '"no_live_target"',
        ):
            self.assertIn(contract, autoplay)

    def test_autoplay_uses_relics_unique_balls_and_upgrades(self) -> None:
        autoplay = source("GameAutoPlay.cpp")

        for contract in (
            "BalanceAutoPlayer::FindRelicToBuy",
            '"save_for_relic"',
            "BalanceAutoPlayer::FindMissingCatalogBall",
            '"fill_missing_ball_type"',
            "BalanceAutoPlayer::FindUpgradeTarget",
            '"concentrate_primary_ball"',
        ):
            self.assertIn(contract, autoplay)


if __name__ == "__main__":
    unittest.main()
