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
            "player_cushion_charge",
			"player_chain_impact",
			"player_refractive_pierce",
			"player_stop_shield",
			"player_pierce_finisher",
			"player_ricochet_finisher",
			"player_anchor_finisher",
            "player_collision_boost",
            "player_lightweight",
            "player_minion_finisher",
            "player_reflection_setup",
            "player_origin_anchor",
        ):
            self.assertGreaterEqual(text.count(f'"{definition_id}"'), 2)
        self.assertIn("GetName", text)
        self.assertIn("GetDescription", text)
        self.assertIn("GetTrait", text)
        self.assertIn('u8"スタンダード球"', text)
        self.assertIn('u8"ヘビー球"', text)
        self.assertIn('u8"貫通球"', text)
        self.assertIn('u8"バウンド球"', text)
        self.assertIn('u8"アンカー球"', text)

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
        # RelicType::Countに対応する11種類の名称と説明が定義される。
        self.assertEqual(text.count("RelicUtf8(u8"), 11 * 2)

    def test_ball_text_is_used_by_each_player_facing_item_screen(self) -> None:
        for file_name in ("GameRewardPresentation.cpp", "ShopScene.cpp", "RestSiteScene.cpp"):
            text = (ROOT / file_name).read_text(encoding="utf-8")
            self.assertIn('include "PlayerBallText.h"', text)
            self.assertIn("PlayerBallText::GetDescription", text)

    def test_ball_card_dynamic_status_uses_explicit_utf8_format_strings(self) -> None:
        presentation = (ROOT / "BallCardPresentation.cpp").read_text(
            encoding="utf-8-sig"
        )
        self.assertIn('PlayerBallText::Utf8(u8"攻撃 %d  /  追突 %d")', presentation)
        self.assertNotIn('sprintf_s(text, "\\u653b\\u6483', presentation)
        self.assertNotIn('sprintf_s(text, "\\u91cd\\u3055', presentation)

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
            "GameRewardPresentation.cpp",
            "GameSaveSystem.cpp",
        ):
            text = (ROOT / file_name).read_text(encoding="utf-8-sig")
            self.assertIn('include "UiText.h"', text)

    def test_combat_feedback_uses_japanese_labels(self) -> None:
        presentation = (ROOT / "GamePresentationCombatEvents.cpp").read_text(encoding="utf-8")

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
