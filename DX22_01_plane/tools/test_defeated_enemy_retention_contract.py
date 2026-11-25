import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def source(name: str) -> str:
    return (ROOT / name).read_text(encoding="utf-8")


class DefeatedEnemyRetentionContractTests(unittest.TestCase):
    def test_defeated_enemy_is_not_destroyed_or_removed_from_physics(self) -> None:
        battle = source("BattleController.cpp")
        collision = source("BallCollisionComponent.cpp")
        enemy = source("EnemyBall.cpp")

        self.assertNotIn("m_World->RequestDestroy(owner)", battle)
        self.assertIn("return enemy->IsEnabled() && !enemy->IsPocketed();", collision)
        self.assertNotIn("RemoveDefeatedPhaseEnemies", battle)
        self.assertIn("m_Ball->GetMutableVelocity() = velocity;", enemy)
        self.assertIn("m_Ball->GetMutableAcceleration() = acceleration;", enemy)

    def test_combat_participation_gate_covers_actions_and_collision_rewards(self) -> None:
        header = source("EnemyBall.h")
        controller = source("BattleController.cpp")
        collision = source("BallCollisionComponent.cpp")
        prediction = source("BallShotPrediction.cpp")

        self.assertIn("bool CanParticipateInCombat() const", header)
        self.assertIn("!enemy->CanParticipateInCombat()", controller)
        self.assertIn("target->CanParticipateInCombat()", collision)
        self.assertIn("if (!ball.enemyId.empty() && ball.defeated) return 0;", prediction)
        self.assertIn("playerHitCombatEnemy || combatEnemyPair", prediction)

    def test_retained_corpse_is_physical_but_not_an_enemy_ability_target(self) -> None:
        collision = source("BallCollisionComponent.cpp")
        prediction = source("BallShotPrediction.cpp")
        rules = source("BallPhysicsRules.h")

        self.assertIn(
            "body.enemy = enemy != nullptr && enemy->CanParticipateInCombat();",
            collision,
        )
        self.assertIn(
            "ball.physics.enemy = enemy->CanParticipateInCombat();",
            prediction,
        )
        self.assertIn("if (aPierces || bPierces)", rules)
        self.assertIn("else\n        {", rules)

    def test_defeat_visual_uses_requested_base_color_adjustment(self) -> None:
        shader = source("shader/billiardBallPS.hlsl")
        enemy = source("EnemyBall.cpp")

        self.assertIn("luminance.xxx, 0.90f", shader)
        self.assertIn("* 0.90f", shader)
        self.assertIn("color.rgb += specular + environment;", shader)
        self.assertIn("kDefeatVisualTransitionSeconds = 0.3f", enemy)

    def test_dead_and_living_enemies_share_pocket_removal(self) -> None:
        collision = source("BallCollisionComponent.cpp")
        prediction = source("BallShotPrediction.cpp")
        game = source("GamePocketLifecycle.cpp")
        self.assertNotIn("BallPhysicsRules::RetainOutsidePocket", collision)
        self.assertNotIn("BallPhysicsRules::RetainOutsidePocket", prediction)
        self.assertIn("OnPocketHit();", collision)
        self.assertIn('else if (!ball.enemyId.empty())', prediction)
        self.assertIn("if (newlyDefeated)", game)
        self.assertIn("World().RequestDestroy(enemy->GetGameObject())", game)

    def test_battle_progress_diagnostics_are_throttled_and_published(self) -> None:
        controller = source("BattleController.cpp")
        mcp = source("GameMcpDiagnosticsState.cpp")

        self.assertIn("m_ShotFixedStepCount % 300 == 0", controller)
        self.assertIn('"battle_progress_diagnostics"', mcp)
        self.assertIn('"non_stopped_balls"', mcp)
        self.assertIn('"continuous_stopped_tick_count"', mcp)
        self.assertIn('"state_history"', mcp)

    def test_defeat_side_effects_are_centralized_and_single_shot(self) -> None:
        enemy = source("EnemyBall.cpp")
        self.assertEqual(enemy.count("GameBattleEvents::EnemyDefeated(this);"), 1)
        self.assertIn("m_DefeatProcessed", enemy)
        self.assertIn("m_PendingChargeInterruption.reset();", enemy)
        self.assertIn("m_Ball->SetStatusEffects({});", enemy)
        self.assertIn("enemy->RefreshBoardAbilityState(true);", enemy)

    def test_presentation_and_mcp_keep_physics_but_hide_combat_state(self) -> None:
        presentation = source("GamePresentationEnemyOverlay.cpp")
        mcp = source("GameMcpCombatState.cpp")
        self.assertIn("enemy->IsDefeated() || enemy->IsPocketed()", presentation)
        self.assertIn('{ "combat_active", combatActive }', mcp)
        self.assertIn("!enemy->CanParticipateInCombat()) continue;", mcp)

    def test_phase_defeat_logging_survives_end_of_turn_removal(self) -> None:
        controller = source("BattleController.cpp")
        game_damage = source("GameDamageAndDefeat.cpp")
        shot_start = source("GameShotStart.cpp")
        game = (source("Game.cpp") + source("GameClearRewardFlow.cpp")
                + source("GamePocketLifecycle.cpp") + source("GameRunCompletion.cpp"))

        self.assertIn("++m_DefeatedEnemyCount;", controller)
        self.assertIn(
            "m_BattleState->controller.NotifyEnemyDefeated();",
            game_damage,
        )
        self.assertIn(
            "m_BattleState->controller.GetDefeatedEnemyCount(),",
            shot_start,
        )
        self.assertGreaterEqual(
            game.count("controller.GetDefeatedEnemyCount()"),
            6,
        )
        self.assertIn("return m_DefeatedEnemyCount;", controller)


if __name__ == "__main__":
    unittest.main()
