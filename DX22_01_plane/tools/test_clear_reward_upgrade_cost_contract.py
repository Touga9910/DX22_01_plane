from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def source(name: str) -> str:
    return (ROOT / name).read_text(encoding="utf-8-sig")


class ClearRewardUpgradeCostContractTests(unittest.TestCase):
    def test_level_zero_and_one_have_the_requested_costs(self) -> None:
        progression = source("GameProgression.cpp")

        self.assertIn("kClearRewardFirstUpgradeCost = 15", progression)
        self.assertIn("kClearRewardSecondUpgradeCost = 30", progression)
        self.assertIn("ball->upgradeLevel == 0", progression)
        self.assertIn("ball->upgradeLevel == 1", progression)

    def test_paid_upgrade_is_limited_to_clear_reward(self) -> None:
        progression = source("GameProgression.cpp")
        paid = progression[
            progression.index("bool Game::ApplyClearRewardUpgrade") :
            progression.index("bool Game::RestUpgradeBall")
        ]

        self.assertIn("m_GameState != GameState::ClearReward", paid)
        self.assertIn("m_PlayerRunStatus.money < cost", paid)
        self.assertIn("RestUpgradeBall(ballIndex)", paid)
        self.assertIn("m_PlayerRunStatus.money -= cost", paid)

    def test_human_autoplay_and_mcp_share_the_paid_path(self) -> None:
        game = source("Game.cpp")
        autoplay = source("GameAutoPlay.cpp")
        bridge = source("GameMcpBridge.cpp")

        self.assertIn("ApplyClearRewardUpgrade(", game)
        self.assertIn("ApplyClearRewardUpgrade(upgradeTarget", autoplay)
        self.assertIn("save_for_upgrade", autoplay)
        self.assertIn("game.ApplyClearRewardUpgrade(", bridge)
        self.assertIn("clear_reward_upgrade_affordable", bridge)

    def test_logs_and_ui_expose_the_cost(self) -> None:
        game = source("Game.cpp")
        autoplay = source("GameAutoPlay.cpp")
        bridge = source("GameMcpBridge.cpp")
        ui = source("UiText.h")

        self.assertIn('rewardDetails["upgrade_cost"]', game)
        self.assertIn('{ "upgrade_cost", chargedCost }', autoplay)
        self.assertIn('rewardDetails["upgrade_cost"]', bridge)
        self.assertIn("UpgradeCostFormat", ui)
        self.assertIn("UpgradeMoneyShortage", ui)


if __name__ == "__main__":
    unittest.main()
