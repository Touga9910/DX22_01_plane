from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class JapaneseItemTextTests(unittest.TestCase):
    def test_all_player_ball_definitions_have_shared_japanese_text(self) -> None:
        text = (ROOT / "PlayerBallText.h").read_text(encoding="utf-8")

        for definition_id in (
            "player_standard",
            "player_heavy",
            "player_pierce",
            "player_bounce",
            "player_anchor",
        ):
            self.assertGreaterEqual(text.count(f'"{definition_id}"'), 2)
        self.assertIn("GetName", text)
        self.assertIn("GetDescription", text)
        self.assertIn("GetTrait", text)

    def test_relic_catalog_no_longer_uses_english_display_text(self) -> None:
        text = (ROOT / "GameTypes.h").read_text(encoding="utf-8")

        for old_text in (
            "Power Core",
            "Guard Core",
            "Impact Accelerator",
            "Emergency Repair Kit",
            "All owned balls gain",
        ):
            self.assertNotIn(old_text, text)
        # 12種類それぞれの名称と説明がUTF-8日本語で定義される。
        self.assertEqual(text.count("RelicUtf8(u8"), 12 * 2)

    def test_ball_text_is_used_by_each_player_facing_item_screen(self) -> None:
        for file_name in ("Game.cpp", "ShopScene.cpp", "RestSiteScene.cpp"):
            text = (ROOT / file_name).read_text(encoding="utf-8")
            self.assertIn('include "PlayerBallText.h"', text)
            self.assertIn("PlayerBallText::GetDescription", text)

    def test_shared_japanese_ui_text_is_used_by_player_facing_screens(self) -> None:
        ui_text = (ROOT / "UiText.h").read_text(encoding="utf-8")
        self.assertIn("namespace UiText", ui_text)
        self.assertIn("ContinueRun", ui_text)
        self.assertIn("RouteWindow", ui_text)
        self.assertIn("ShopWindow", ui_text)
        self.assertIn("RestWindow", ui_text)
        self.assertIn("ClearWindow", ui_text)

        for file_name in (
            "TitleScene.cpp",
            "StageSelectScene.cpp",
            "ShopScene.cpp",
            "RestSiteScene.cpp",
            "Game.cpp",
            "GameSaveSystem.cpp",
        ):
            text = (ROOT / file_name).read_text(encoding="utf-8-sig")
            self.assertIn('include "UiText.h"', text)

    def test_combat_feedback_uses_japanese_labels(self) -> None:
        presentation = (ROOT / "GamePresentation.cpp").read_text(encoding="utf-8")

        for old_label in (
            '"BREAK!"',
            '"BLOCK"',
            '"SCRATCH"',
            '"POCKET FINISH!"',
            '"POCKET CONTROL"',
            '"PLAYER "',
        ):
            self.assertNotIn(old_label, presentation)
        self.assertIn("playerHudMessage", presentation)


if __name__ == "__main__":
    unittest.main()
