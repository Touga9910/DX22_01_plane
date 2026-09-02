from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def source(name: str) -> str:
    return (ROOT / name).read_text(encoding="utf-8-sig")


class DebugCombatForecastContractTests(unittest.TestCase):
    def test_forecast_recalculates_only_while_dirty(self) -> None:
        game = source("Game.cpp")
        draw = game[
            game.index("void Game::Draw()") :
            game.index("void Game::DrawPauseUI()")
        ]

        self.assertIn("m_DebugCombatForecastDirty", draw)
        self.assertIn("RefreshDebugCombatForecast();", draw)
        self.assertIn("if (!m_DebugCombatForecastDirty)", game)
        self.assertNotIn(
            'InvalidateDebugCombatForecast("シーン変更")',
            game[game.index("void Game::Update()") : game.index("void Game::Draw()")],
        )

    def test_damage_changes_invalidate_the_cached_forecast(self) -> None:
        progression = source("GameProgression.cpp")
        battle = source("BattleScene.cpp")
        autoplay = source("GameAutoPlay.cpp")

        for reason in (
            "エネミー被ダメージ",
            "プレイヤー被ダメージ",
            "プレイヤーHP回復",
            "プレイヤー防御力変更",
            "エネミーポケット",
        ):
            self.assertIn(reason, progression)
        self.assertIn("エネミーステータス再読込", battle)
        self.assertIn("戦闘開始", autoplay)

    def test_phase_and_damage_breakdown_are_visible(self) -> None:
        game = source("Game.cpp")
        header = source("Game.h")

        for label in (
            "このターンの敵攻撃前",
            "敵攻撃処理中",
            "このターンの敵攻撃は処理済み",
            "理論: %d  実HP減少: %d  超過: %d",
            "最低保証",
        ):
            self.assertIn(label, game)
        for field in (
            "theoreticalDamage",
            "overkillDamage",
            "damageBeforeMinimum",
            "minimumDamageApplied",
        ):
            self.assertIn(field, header)

    def test_prediction_comparison_history_and_filters_exist(self) -> None:
        game = source("Game.cpp")
        header = source("Game.h")

        for label in (
            "直近敵攻撃",
            "直近の被ダメージ履歴",
            "攻撃予定のみ",
            "撃破済み",
            "ポケット中",
            "敵の並び順",
        ):
            self.assertIn(label, game)
        self.assertIn("ImGui::ProgressBar", game)
        self.assertIn("RecordDebugPlayerDamage", game)
        self.assertIn("m_DebugPlayerDamageHistory", header)
        self.assertIn("m_DebugLastEnemyAttackPredictedDamage", header)

    def test_real_and_preview_damage_share_the_same_formula(self) -> None:
        status = source("BallStatusComponent.h")
        player = source("PlayerBall.h")
        game = source("Game.cpp")

        self.assertIn("int CalculateDamageTaken(int damage) const", status)
        self.assertIn("const int finalDamage = CalculateDamageTaken(damage);", status)
        self.assertIn("CalculateDamageTaken(int damage) const", player)
        self.assertIn("player->CalculateDamageTaken(enemy->GetAttack())", game)


if __name__ == "__main__":
    unittest.main()
