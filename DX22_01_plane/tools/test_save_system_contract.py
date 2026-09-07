from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def source(name: str) -> str:
    return (ROOT / name).read_text(encoding="utf-8")


class SaveSystemContractTests(unittest.TestCase):
    def test_save_document_is_versioned_checked_and_atomically_committed(self) -> None:
        manager = source("GameSaveManager.cpp") + source("PlayerBallSaveData.h")

        self.assertIn("constexpr int kSchemaVersion = 1", manager)
        self.assertIn("CalculateChecksum(payload)", manager)
        self.assertIn('kSavePath.string() + ".tmp"', manager)
        self.assertIn("MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH", manager)

    def test_run_deck_relic_and_random_state_are_persisted(self) -> None:
        manager = source("GameSaveManager.cpp") + source("PlayerBallSaveData.h")

        for key in (
            '"current_hp"',
            '"money"',
            '"owned_relics"',
            '"draw_pile"',
            '"upgrade_level"',
            '"stage_selector_state"',
            '"route_selection_counter"',
            '"dynamic_balance"',
        ):
            self.assertIn(key, manager)

    def test_safe_scene_transitions_autosave_and_f6_is_manual_save(self) -> None:
        game = source("Game.cpp")
        save_system = source("GameSaveSystem.cpp")

        self.assertIn("Input::GetKeyTrigger(VK_F6)", game)
        self.assertIn("SaveRunCheckpoint(sceneType, false, false)", game)
        self.assertIn("UiText::BattleAutosaveNotice", save_system)

    def test_title_exposes_continue_and_terminal_results_remove_save(self) -> None:
        title = source("TitleScene.cpp")
        game = source("Game.cpp")

        self.assertIn("UiText::ContinueRun", title)
        self.assertIn("LoadSavedRun()", title)
        self.assertGreaterEqual(game.count("GameSaveManager::Remove();"), 2)


if __name__ == "__main__":
    unittest.main()
