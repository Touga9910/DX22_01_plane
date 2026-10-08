from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def source(name: str) -> str:
    return (ROOT / name).read_text(encoding="utf-8-sig")


class ShopBallOfferContractTests(unittest.TestCase):
    def test_shop_rolls_and_displays_three_ball_offers(self) -> None:
        game_header = source("Game.h")
        progression = source("GameProgression.cpp")
        scene = source("ShopScene.cpp")

        self.assertIn("kShopBallOfferSize = 3", game_header)
        self.assertIn(
            "RollDistinctCatalogOfferIndices(kShopBallOfferSize)",
            progression,
        )
        self.assertIn("GameSceneCommands::RollShopBallOffers()", scene)
        self.assertIn("view.shopBallOfferCount", scene)
        self.assertIn("GameView::ShopBallOffer(index)", scene)

    def test_purchase_maps_offer_index_to_catalog_index(self) -> None:
        progression = source("GameProgression.cpp")
        scene = source("ShopScene.cpp")

        self.assertIn(
            "BuyShopBall(GetShopBallOfferCatalogIndex(offerIndex), cost)",
            progression,
        )
        self.assertIn("BuyShopBallOffer(m_SelectedBuyBall", scene)

    def test_shop_ball_offers_round_trip_through_run_save(self) -> None:
        save_state = source("GameRunSaveState.h")
        save_bridge = source("GameSaveBridge.cpp")
        save_manager = source("GameSaveManager.cpp")

        self.assertIn("shopBallOffers", save_state)
        self.assertIn("snapshot.shopBallOffers", save_bridge)
        self.assertIn('"shop_ball_offers"', save_manager)
        self.assertIn("savedDeckRandomEngine", save_bridge)


if __name__ == "__main__":
    unittest.main()
