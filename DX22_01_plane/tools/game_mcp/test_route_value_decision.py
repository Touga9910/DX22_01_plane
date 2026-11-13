from __future__ import annotations

import copy
import unittest
from pathlib import Path

from build_decision import evaluate_shop_actions
from build_profiles import load_build_profiles
from route_decision import evaluate_rest_site_action, evaluate_route_options


def ball(index: int, definition_id: str, *, attack: int = 2,
         can_upgrade: bool = True, level: int = 0) -> dict:
    return {
        "index": index,
        "instance_id": index + 1,
        "definition_id": definition_id,
        "upgrade_level": level,
        "can_upgrade": can_upgrade,
        "status": {"attack": attack, "mass": 2.0, "restitution": 0.8},
    }


def route_state(hp: int = 50, money: int = 50) -> dict:
    nodes = [
        {"node_id": 0, "destination": "battle", "next_node_ids": [3], "reachable": True},
        {"node_id": 1, "destination": "rest", "next_node_ids": [4], "reachable": True},
        {"node_id": 2, "destination": "shop", "next_node_ids": [5], "reachable": True},
        {"node_id": 3, "destination": "battle", "next_node_ids": [6], "reachable": True},
        {"node_id": 4, "destination": "battle", "next_node_ids": [6], "reachable": True},
        {"node_id": 5, "destination": "battle", "next_node_ids": [6], "reachable": True},
        {"node_id": 6, "destination": "shop", "next_node_ids": [], "reachable": True},
        # Exists in the map, but no offered path reaches it.
        {"node_id": 99, "destination": "shop", "next_node_ids": [], "reachable": False},
    ]
    return {
        "scene": "stage_select",
        "player": {"current_hp": hp, "max_hp": 50, "money": money},
        "deck_balls": [ball(index, "player_standard") for index in range(6)],
        "route_options": [
            {"route_index": 0, "node_id": 0, "destination": "battle", "next_node_ids": [3]},
            {"route_index": 1, "node_id": 1, "destination": "rest", "next_node_ids": [4]},
            {"route_index": 2, "node_id": 2, "destination": "shop", "next_node_ids": [5]},
        ],
        "run_map": {"nodes": nodes, "path": []},
        "relics": [],
        "shop_ball_offers": [],
    }


class RouteValueDecisionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.profiles, _ = load_build_profiles(
            Path(__file__).resolve().parent / "build_profiles.json"
        )

    def test_full_hp_rest_is_valuable_when_free_upgrade_exists(self) -> None:
        state = route_state(hp=50, money=0)
        state["deck_balls"] = state["deck_balls"][:5]
        decision = evaluate_rest_site_action(state, self.profiles, "standard")
        self.assertEqual(decision["recommended_action"], "upgrade_ball")
        self.assertGreater(decision["free_upgrade_value"], 0)
        routes = evaluate_route_options(state, self.profiles, "standard")
        self.assertEqual(routes["selected"]["destination"], "rest")
        rest = next(item for item in routes["routes"] if item["destination"] == "rest")
        self.assertGreater(rest["values"]["free_upgrade_value"], 0)

    def test_non_dangerous_recovery_still_has_value(self) -> None:
        decision = evaluate_rest_site_action(
            route_state(hp=40), self.profiles, "standard",
        )
        self.assertGreaterEqual(decision["recovery_amount"], 10)
        self.assertGreater(decision["recovery_value"], 0)

    def test_danger_hp_forces_immediate_rest_safety(self) -> None:
        routes = evaluate_route_options(
            route_state(hp=15), self.profiles, "standard",
        )
        self.assertEqual(routes["selected"]["destination"], "rest")
        self.assertTrue(routes["selected"]["safety_eligible"])
        self.assertFalse(next(
            item for item in routes["routes"] if item["destination"] == "battle"
        )["safety_eligible"])

    def test_money_and_redundant_standard_balls_value_shop(self) -> None:
        routes = evaluate_route_options(
            route_state(money=50), self.profiles, "pierce",
        )
        shop = next(item for item in routes["routes"] if item["destination"] == "shop")
        self.assertGreater(shop["values"]["deck_compression_value"], 0)

    def test_insufficient_money_has_no_compression_value(self) -> None:
        routes = evaluate_route_options(
            route_state(money=14), self.profiles, "pierce",
        )
        shop = next(item for item in routes["routes"] if item["destination"] == "shop")
        self.assertEqual(shop["values"]["deck_compression_value"], 0)

    def test_missing_generator_shop_offer_is_purchased(self) -> None:
        state = route_state(money=20)
        state.update({
            "scene": "shop",
            "available_actions": ["buy_ball", "continue_to_battle"],
            "shop_ball_offers": [
                {**ball(0, "player_pierce"), "offer_index": 0, "price": 20},
            ],
        })
        plan = evaluate_shop_actions(state, self.profiles, "pierce")
        self.assertEqual(plan["recommended"]["action"], "buy_ball")
        self.assertEqual(plan["recommended"]["definition_id"], "player_pierce")

    def test_redundant_standard_offer_is_not_purchased(self) -> None:
        state = route_state(money=20)
        state.update({
            "scene": "shop",
            "available_actions": ["buy_ball", "continue_to_battle"],
            "deck_balls": state["deck_balls"][:5],
            "shop_ball_offers": [
                {**ball(0, "player_standard"), "offer_index": 0, "price": 20},
            ],
        })
        plan = evaluate_shop_actions(state, self.profiles, "standard")
        self.assertFalse(any(
            action["action"] == "buy_ball"
            for action in plan["ranked_actions"]
        ))

    def test_future_shop_can_make_generator_purchase_precede_removal(self) -> None:
        state = route_state(money=20)
        state["scene"] = "shop"
        state["available_actions"] = ["buy_ball", "remove_ball", "continue_to_battle"]
        state["shop_ball_offers"] = [
            {**ball(0, "player_pierce"), "offer_index": 0, "price": 20},
        ]
        state["run_map"]["nodes"][2]["active"] = True
        plan = evaluate_shop_actions(state, self.profiles, "pierce")
        self.assertTrue(plan["future_shop_reachable"])
        self.assertEqual(plan["recommended"]["action"], "buy_ball")

    def test_critical_core_relic_can_override_default_shop_order(self) -> None:
        state = route_state(money=50)
        state["scene"] = "shop"
        state["available_actions"] = [
            "buy_ball", "remove_ball", "buy_relic", "continue_to_battle",
        ]
        state["relics"] = [{
            "index": 0,
            "name": "Power Core",
            "price": 20,
            "owned": False,
            "shop_offered": True,
        }]
        plan = evaluate_shop_actions(state, self.profiles, "standard")
        self.assertEqual(plan["recommended"]["action"], "buy_relic")
        self.assertEqual(plan["recommended"]["reason"], "critical_core_relic")

    def test_unconnected_shop_is_not_counted(self) -> None:
        state = route_state()
        state["route_options"] = [state["route_options"][0]]
        evaluation = evaluate_route_options(state, self.profiles, "standard")
        route = evaluation["routes"][0]
        self.assertNotIn(99, {
            entry["node_id"] for entry in route["reachable_shop_nodes"]
        })

    def test_future_inventory_is_not_treated_as_known(self) -> None:
        state = route_state()
        baseline = evaluate_route_options(state, self.profiles, "pierce")
        with_fake_stock = copy.deepcopy(state)
        with_fake_stock["shop_ball_offers"] = [
            {**ball(0, "player_pierce"), "offer_index": 0},
        ]
        compared = evaluate_route_options(with_fake_stock, self.profiles, "pierce")
        self.assertEqual(
            [route["score"] for route in baseline["routes"]],
            [route["score"] for route in compared["routes"]],
        )


if __name__ == "__main__":
    unittest.main()
