import unittest

from server import _shop_has_actionable_purchase


class MultipleRelicPurchaseTests(unittest.TestCase):
    def state(self, money=100, second_owned=False, offered=True):
        return {
            'scene': 'shop',
            'player': {'money': money},
            # A stale pre-change snapshot must not disable the remaining stock.
            'relic_selection': {'shop_purchase_used': True},
            'deck_rule': {'can_remove': False},
            'relics': [
                {'owned': True, 'shop_offered': True, 'price': 30},
                {'owned': second_owned, 'shop_offered': offered, 'price': 40},
            ],
        }

    def test_second_affordable_relic_remains_actionable(self):
        self.assertTrue(_shop_has_actionable_purchase(self.state(money=40)))

    def test_money_ownership_and_stock_still_apply(self):
        for state in (self.state(money=39), self.state(second_owned=True), self.state(offered=False)):
            with self.subTest(state=state):
                self.assertFalse(_shop_has_actionable_purchase(state))


if __name__ == '__main__':
    unittest.main()
