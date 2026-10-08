from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def source(name: str) -> str:
    return (ROOT / name).read_text(encoding="utf-8-sig")


class GameResponsibilitySplitContractTests(unittest.TestCase):
    def test_shot_prediction_consumes_explicit_input_snapshot(self):
        prediction_header = source("BallShotPrediction.h")
        prediction_source = source("BallShotPrediction.cpp")
        game_bridge = source("GameBattleBridge.cpp")

        self.assertNotIn("class Game;", prediction_header)
        self.assertNotIn("Predict(Game&", prediction_header)
        self.assertIn("struct Input", prediction_header)
        self.assertNotIn('#include "Game.h"', prediction_source)
        self.assertNotIn("Game::GetInstance", prediction_source)
        self.assertIn("Capture(const Input& input)", prediction_source)
        self.assertIn("input.cushionCharges", prediction_source)
        self.assertIn("input.tableFrames", prediction_source)
        self.assertIn("input.pockets", prediction_source)
        self.assertIn("MakeShotPredictionInput", game_bridge)
        self.assertIn("input.shot = game.MakePredictionShotRules", game_bridge)
        self.assertNotIn("game.GetComponents", prediction_source)
        self.assertIn("GameRuntime::World().GetComponents", game_bridge)
        self.assertNotIn("game.HasRelic", prediction_source)
        self.assertIn("game.HasRelic", game_bridge)
        for getter in (
            "GetCushionCharges",
            "GetPierceTraceState",
            "GetCurrentPlayerBallData",
            "GetCurrentPocketFinisherRatio",
            "GetPlayerPocketDamageAmount",
            "GetPierceMaximumUses",
            "GetPierceSpeedRetention",
            "GetRelicAttackBonus",
            "GetHeavyCollisionCount",
            "GetPlayerAnchorStacks",
            "WasCushionBoostConsumedThisShot",
            "GetCurrentShotPlayerEnemyCollisionCount",
            "GetCurrentShotEnemyEnemyCollisionCount",
        ):
            self.assertNotIn(f"game.{getter}", prediction_source)
            self.assertIn(f"game.{getter}", game_bridge)

    def test_ball_physics_world_uses_explicit_runtime_context(self):
        physics_header = source("BallPhysicsWorld.h")
        physics_source = source("BallPhysicsWorld.cpp")
        game_source = source("Game.cpp")

        self.assertNotIn("class Game;", physics_header)
        self.assertNotIn("Step(Game&", physics_header)
        self.assertIn("struct BallPhysicsWorldContext", physics_header)
        self.assertNotIn('#include "Game.h"', physics_source)
        self.assertNotIn("Game::GetInstance", physics_source)
        self.assertIn("context.world.GetComponents", physics_source)
        self.assertIn("context.shouldContinue()", physics_source)
        self.assertIn("context.traceMovement", physics_source)
        self.assertIn("BallPhysicsWorldContext physicsContext", game_source)
        self.assertIn("BallPhysicsWorld::Step(physicsContext)", game_source)

    def test_player_ball_uses_typed_gameplay_boundaries(self):
        player_ball = source("PlayerBall.cpp")
        battle_bridge = source("GameBattleBridge.cpp")

        self.assertNotIn('#include "Game.h"', player_ball)
        self.assertNotIn("Game::GetInstance", player_ball)
        self.assertIn("GameRuntime::MainCamera()", player_ball)
        self.assertIn("GameRuntime::World().GetComponents", player_ball)
        self.assertIn("GameBattleView::State()", player_ball)
        self.assertIn("GameBattleCommands::BeginPowerSelection()", player_ball)
        self.assertIn("GameBattleEvents::PlayerShotFired", player_ball)
        self.assertIn("GamePlayerState::CaptureFrom", player_ball)
        self.assertIn("GameShotPrediction::Predict", player_ball)
        self.assertIn("GameShotTraceView::Current", player_ball)
        self.assertIn("BallShotPrediction::Predict(", battle_bridge)
        self.assertIn("void GameBattleEvents::PlayerShotFired", battle_bridge)
        self.assertNotIn('#include "PierceTraceRules.h"', source("GameBattleView.h"))

    def test_enemy_ball_uses_narrow_runtime_and_battle_event_boundaries(self):
        enemy_ball = source("EnemyBall.cpp")
        battle_bridge = source("GameBattleBridge.cpp")

        self.assertNotIn('#include "Game.h"', enemy_ball)
        self.assertNotIn("Game::GetInstance", enemy_ball)
        self.assertIn("GameRuntime::MainCamera()", enemy_ball)
        self.assertIn("GameRuntime::World()", enemy_ball)
        self.assertIn("GameBattleEvents::EnemyPocketed", enemy_ball)
        self.assertIn("GameBattleEvents::EnemyDefeated", enemy_ball)
        self.assertIn("GameBattleEvents::CombatFeedback", enemy_ball)
        self.assertIn("void GameBattleEvents::EnemyPocketed", battle_bridge)
        self.assertIn("void GameBattleEvents::EnemyDefeated", battle_bridge)

    def test_game_header_keeps_heavy_implementation_headers_private(self) -> None:
        game = source("Game.h")

        self.assertIn('#include "json/json_fwd.hpp"', game)
        for implementation_header in (
            '#include "json/json.hpp"',
            '#include "BossShotPlanner.h"',
            '#include "GameDebugController.h"',
            '#include "RunProgressController.h"',
        ):
            self.assertNotIn(implementation_header, game)

    def test_battle_state_and_turn_flow_are_owned_by_battle_controller(self) -> None:
        game = source("Game.h")
        battle = source("BattleController.h") + source("BattleController.cpp")

        self.assertNotIn("enum class GameState", game)
        self.assertNotIn("m_GameState", game)
        self.assertIn("BattleState m_State", battle)
        self.assertIn("void BattleController::ProcessEnemyAttack()", battle)
        self.assertIn("void BattleController::ProcessTurnEnd()", battle)
        self.assertIn("BattleResult BattleController::ConsumeResult()", battle)
        for battle_member in (
            "m_ShotRelicRules",
            "m_PocketedEnemyQueue",
            "m_StageType",
            "m_BountyRewardClaimed",
        ):
            self.assertIn(battle_member, battle)
            self.assertNotIn(battle_member, game)
        for old_shot_member in (
            "m_CurrentShotCollisionAttackBonus",
            "m_CurrentShotPlayerEnemyCollisionCount",
            "m_CurrentShotEnemyEnemyCollisionCount",
            "m_CurrentShotBankShotReady",
            "m_CurrentShotLaunchPower",
        ):
            self.assertNotIn(old_shot_member, game)

    def test_run_state_is_owned_and_mutated_by_run_controller(self) -> None:
        game = source("Game.h")
        run = source("RunController.h") + source("RunController.cpp")

        self.assertIn("std::unique_ptr<RunController> m_RunController", game)
        self.assertNotIn("PlayerRunStatus m_PlayerRunStatus", game)
        self.assertNotIn("PlayerDeck m_PlayerDeck", game)
        self.assertNotIn("RunProgressController m_RunProgress", game)
        self.assertIn("struct RunController::Impl", run)
        self.assertIn("PlayerRunStatus status", run)
        self.assertIn("PlayerDeck deck", run)
        self.assertIn("RunProgressController progress", run)
        self.assertIn("std::unique_ptr<Impl> m_Impl", source("RunController.h"))
        for hidden_include in (
            '#include "PlayerDeck.h"',
            '#include "PlayerRunStatus.h"',
            '#include "RunProgressController.h"',
            '#include "RunProgressConstants.h"',
            '#include "StageSelector.h"',
        ):
            self.assertNotIn(hidden_include, source("RunController.h"))
        self.assertIn("bool RunController::RestHeal()", run)
        self.assertIn("bool RunController::BuyShopBall", run)
        self.assertIn("bool RunController::BuyRelic", run)
        self.assertIn("bool RunController::CollectStageReward", run)

    def test_autoplay_state_and_decisions_are_owned_by_balance_auto_player(self) -> None:
        game = source("Game.h")
        runtime = source("GameBalanceRuntime.h")
        autoplay = source("BalanceAutoPlayer.h") + source("GameAutoPlay.cpp")

        self.assertIn("BalanceAutoPlayer autoPlayer", runtime)
        self.assertNotIn("BalanceAutoPlayer m_BalanceAutoPlayer", game)
        self.assertNotIn("bool UpdateBalanceAutoPlay();", game)
        self.assertNotIn("void LoadBalanceAutoPlayConfig(", game)
        for old_member in (
            "m_BalanceAutoPlayEnabled",
            "m_AutoRestartAfterGameOver",
            "m_BalanceAutoDecisionFrame",
            "m_BalanceAutoRandom",
            "m_BalanceAutoPendingBallAdjustments",
        ):
            self.assertNotIn(old_member, game)

        for decision in (
            "bool BalanceAutoPlayer::Update(Game& game)",
            "void BalanceAutoPlayer::SelectBall(Game& game)",
            "bool BalanceAutoPlayer::FireShot(Game& game)",
            "void BalanceAutoPlayer::ApplyReward(Game& game)",
        ):
            self.assertIn(decision, autoplay)

        self.assertIn("game.ApplyBalanceAutoBallSelection", autoplay)
        self.assertIn("game.MarkBalanceAutoRewardChosen", autoplay)

    def test_balance_system_state_is_owned_by_controllers(self) -> None:
        game = source("Game.h")
        runtime = source("GameBalanceRuntime.h")
        dynamic = source("DynamicBalanceController.h") + source(
            "DynamicBalanceController.cpp"
        )
        validation = source("BalanceValidationController.h") + source(
            "BalanceValidationController.cpp"
        )

        self.assertIn("DynamicBalanceController scaling", runtime)
        self.assertIn("BalanceValidationController validation", runtime)
        self.assertNotIn("DynamicBalanceController m_DynamicBalanceController", game)
        self.assertNotIn(
            "BalanceValidationController m_BalanceValidationController", game
        )
        for old_member in (
            "m_DynamicBalanceEnabled",
            "m_DynamicBalanceLevel",
            "m_DynamicBalanceStageShots",
            "m_BalanceValidationEnabled",
            "m_BalanceValidationSeeds",
            "m_BalanceValidationVariants",
        ):
            self.assertNotIn(old_member, game)

        self.assertIn(
            "int DynamicBalanceController::CalculateProgressionHpModifier(",
            dynamic,
        )
        self.assertIn(
            "int DynamicBalanceController::CalculateProgressionAttackModifier(",
            dynamic,
        )
        for retired_event in (
            "DynamicBalanceController::OnRunStarted",
            "DynamicBalanceController::OnBattleStarted",
            "DynamicBalanceController::OnShotStarted",
            "DynamicBalanceController::OnHit",
            "DynamicBalanceController::OnShotFinished",
            "DynamicBalanceController::OnBattleFinished",
        ):
            self.assertNotIn(retired_event, dynamic)
        self.assertIn(
            "BalanceValidationRun BalanceValidationController::OnRunStarted(",
            validation,
        )

    def test_debug_state_and_workflows_are_owned_by_debug_controller(self) -> None:
        game = source("Game.h")
        debug = (
            source("GameDebugController.h")
            + source("GameDebugMode.cpp")
            + source("GameStageEditor.cpp")
            + source("Game.cpp")
        )

        self.assertIn("std::unique_ptr<GameDebugController> m_DebugController", game)
        self.assertNotIn('#include "GameDebugController.h"', game)
        for old_member in (
            "bool m_DebugMode",
            "bool m_DebugEditorOpen",
            "DebugBattleSetup m_DebugSetup",
            "std::vector<EnemyData> m_DebugEnemyCatalog",
            "StageLayoutEditor m_StageEditor",
            "DebugCombatForecastSnapshot m_DebugCombatForecast",
            "std::deque<DebugPlayerDamageRecord> m_DebugPlayerDamageHistory",
        ):
            self.assertNotIn(old_member, game)

        for workflow in (
            "void GameDebugController::Open(Game& game)",
            "bool GameDebugController::StartBattle(Game& game)",
            "void GameDebugController::FinishBattle(Game& game, bool victory)",
            "void GameDebugController::SavePreset()",
            "bool GameDebugController::LoadPreset()",
            "void GameDebugController::RefreshCombatForecast(Game& game)",
            "void GameDebugController::RecordPlayerDamage(",
            "void GameDebugController::DrawDiagnostics(Game& game)",
            "void GameDebugController::DrawStageEditor(Game&)",
        ):
            self.assertIn(workflow, debug)

    def test_player_facing_draw_workflows_are_owned_by_presentation(self) -> None:
        game_header = source("Game.h")
        game_source = source("Game.cpp")
        presentation_header = source("GamePresentation.h")
        presentation = presentation_header + game_source

        self.assertIn("std::unique_ptr<GamePresentation> m_GamePresentation", game_header)
        for workflow in (
            "PausePresentationIntent GamePresentation::DrawPause(",
            "BallSelectionPresentationIntent GamePresentation::DrawBallSelection(",
            "ClearRewardPresentationIntent GamePresentation::DrawClearReward(",
        ):
            self.assertIn(workflow, presentation)
        self.assertNotIn("friend class GamePresentation", game_header)
        self.assertNotIn("game.m_", source("GamePresentation.cpp"))
        self.assertIn("PausePresentationModel", presentation_header)
        self.assertIn("BallSelectionPresentationModel", presentation_header)
        self.assertIn("ClearRewardPresentationModel", presentation_header)
        for old_workflow in (
            "void Game::DrawPauseUI()",
            "void Game::DrawBallSelectionUI()",
            "void Game::DrawClearRewardUI()",
        ):
            self.assertNotIn(old_workflow, game_source)

    def test_boss_shot_search_is_owned_by_planner(self) -> None:
        game = source("Game.h")
        planner = source("BossShotPlanner.h") + source("GameBossAI.cpp")

        self.assertIn("std::unique_ptr<BossShotPlanner> m_BossShotPlanner", game)
        self.assertNotIn('#include "BossShotPlanner.h"', game)
        self.assertNotIn("friend class BossShotPlanner", game)
        self.assertNotIn("m_BossShotCache", game)
        self.assertNotIn("m_BossShotCacheKey", game)
        self.assertIn("json BossShotPlanner::Evaluate(Game& game)", planner)
        self.assertIn("bool BossShotPlanner::Fire(Game& game", planner)

    def test_automated_shot_sources_share_the_validated_game_boundary(self) -> None:
        game = source("Game.h") + source("GameBossAI.cpp")
        autoplay = source("GameAutoPlay.cpp")
        mcp = source("GameMcpBridge.cpp")

        self.assertIn("bool Game::TryFireAutomatedShot(", game)
        self.assertIn("GetBattleState() != BattleState::AimingDirection", game)
        self.assertIn("!AreAllBallsStopped()", game)
        self.assertIn("game.TryFireAutomatedShot(", autoplay)
        self.assertIn("game.TryFireAutomatedShot(", mcp)
        self.assertNotIn("friend class BalanceAutoPlayer", source("Game.h"))

    def test_save_uses_typed_owner_snapshots(self) -> None:
        game = source("Game.h")
        deck = source("PlayerDeck.h")
        selector = source("StageSelector.h")
        save = source("GameSaveManager.cpp")
        manager_header = source("GameSaveManager.h")
        system = source("GameSaveSystem.cpp")
        bridge = source("GameSaveBridge.cpp")
        boundary = source("GameRunSaveState.h")

        self.assertIn("PlayerDeckSnapshot CaptureSnapshot() const", deck)
        self.assertIn("bool RestoreSnapshot(PlayerDeckSnapshot snapshot)", deck)
        self.assertIn("StageSelectorSnapshot CaptureSnapshot() const", selector)
        self.assertNotIn("friend class GameSaveManager", game + deck + selector)
        self.assertNotIn("game.m_", save)
        self.assertIn("GameRunSaveSnapshot", boundary)
        self.assertIn("GameRunRestoreRequest", boundary)
        self.assertNotIn('#include "Game.h"', save)
        self.assertNotIn("class Game;", manager_header)
        self.assertIn("const GameRunSaveSnapshot& snapshot", manager_header)
        self.assertIn("const GameRunLoadRules& rules", manager_header)
        self.assertNotIn("game.", save)
        self.assertIn("CaptureRunSaveSnapshot()", system)
        self.assertIn("RestoreRunSaveSnapshot(std::move(request))", system)
        self.assertIn("GameRunSaveSnapshot Game::CaptureRunSaveSnapshot() const", bridge)
        self.assertIn("void Game::RestoreRunSaveSnapshot(", bridge)
        self.assertIn("m_RunController->Deck().CaptureSnapshot()", bridge)
        self.assertIn("m_RunController->Deck().RestoreSnapshot(", bridge)

    def test_mcp_state_serialization_uses_a_read_only_snapshot(self) -> None:
        game = source("Game.h")
        bridge = source("GameMcpBridge.cpp")
        boundary = source("GameMcpState.h")
        build_state = bridge[
            bridge.index("nlohmann::json GameMcpBridge::BuildState"):
            bridge.index("void GameMcpBridge::PublishState")
        ]

        self.assertIn("struct GameMcpReadSnapshot", boundary)
        self.assertIn("static GameMcpReadSnapshot Capture(const Game& game)", boundary)
        self.assertIn("friend class GameMcpReadView", game)
        self.assertIn("GameMcpReadSnapshot GameMcpReadView::Capture(const Game& game)", bridge)
        self.assertIn("GameMcpReadView::Capture(game)", build_state)
        self.assertNotIn("game.m_", build_state)
        self.assertIn('state["balance_validation"]', build_state)
        self.assertIn('state["stage_layout_control"]', build_state)
        self.assertIn('state["debug_mode"]', build_state)

    def test_mcp_writes_use_named_commands_instead_of_bridge_friend_access(self) -> None:
        game = source("Game.h")
        bridge = source("GameMcpBridge.cpp")
        boundary = source("GameMcpCommands.h")
        execute_command = bridge[
            bridge.index("nlohmann::json GameMcpBridge::ExecuteCommand"):
            bridge.index("void GameMcpBridge::ProcessPendingCommand")
        ]

        self.assertNotIn("friend class GameMcpBridge", game)
        self.assertIn("friend class GameMcpCommands", game)
        self.assertIn("class GameMcpCommands final", boundary)
        self.assertNotIn("game.m_", execute_command)
        for command in (
            "InspectStageLayout",
            "ProposeStageLayout",
            "QueueNextStageOverride",
            "ClearNextStageOverride",
            "PrunePendingBallAdjustments",
            "DisableAutoPlayForExternalControl",
            "HasValidationVariant",
            "ApplyExtraMoneyReward",
        ):
            self.assertIn(f"GameMcpCommands::{command}", bridge)

        self.assertIn("game.ApplyBalanceAutoBallSelection(offerIndex)", execute_command)
        self.assertIn("FindDeckBallIndex(game, instanceId)", execute_command)
        self.assertIn("game.NotifyBallAcquired(selectedBallId)", execute_command)
        self.assertIn("game.MarkBalanceAutoRewardChosen(", execute_command)

    def test_debug_controller_uses_typed_snapshot_and_host_commands(self) -> None:
        game = source("Game.h")
        controller_header = source("GameDebugController.h")
        host_header = source("GameDebugHost.h")
        implementation = source("GameDebugMode.cpp")
        diagnostics = source("Game.cpp")
        controller_implementation = implementation[
            implementation.index("void GameDebugController::Open"):
        ]

        self.assertNotIn("friend class GameDebugController", game)
        self.assertIn("friend class GameDebugHost", game)
        self.assertIn("struct DebugRunSnapshot", controller_header)
        self.assertIn("ProgressionProfile progressionProfile", controller_header)
        self.assertIn("std::optional<DebugRunSnapshot> m_DebugPreviousRun", controller_header)
        self.assertNotIn("game.m_", controller_implementation)
        self.assertNotIn("game.m_", diagnostics)
        for command in (
            "CaptureRunSnapshot",
            "PrepareSession",
            "ApplyRunSettings",
            "StartBattle",
            "EndSession",
            "CaptureCurrentPlayerStatus",
            "SetPaused",
            "UpdateExternalBridge",
            "CaptureAutomation",
            "SetAutoPlayEnabled",
            "SetStopAfterCurrentRunRequested",
            "SaveSnapshot",
        ):
            self.assertIn(f"GameDebugHost::{command}", implementation)

        self.assertIn("game.GetCurrentSceneType() == SceneType::Title", implementation)
        self.assertNotIn("dynamic_cast<TitleScene*>", implementation)

    def test_game_header_hides_balance_automation_runtime_details(self) -> None:
        game = source("Game.h")
        runtime = source("GameBalanceRuntime.h")
        runtime_source = source("GameBalanceConfig.cpp")
        debug = source("GameDebugMode.cpp")
        mcp = source("GameMcpBridge.cpp")

        self.assertIn("std::unique_ptr<GameBalanceRuntime> m_BalanceRuntime", game)
        for dependency in (
            '#include "BalanceAutoPlayer.h"',
            '#include "BalanceValidationController.h"',
            '#include "DynamicBalanceController.h"',
        ):
            self.assertNotIn(dependency, game)
            self.assertIn(dependency, runtime)

        for state in (
            "BalanceAutoPlayer autoPlayer",
            "BalanceValidationController validation",
            "DynamicBalanceController scaling",
            "std::uint32_t runRandomSeed",
            "std::uint32_t stageSelectionSeed",
            "std::uint32_t routeSelectionSeed",
            "std::string baselineDifficultyProfile",
        ):
            self.assertIn(state, runtime)

        self.assertIn("bool IsBalanceAutoPlayEnabled() const;", game)
        self.assertIn("bool IsEnduranceMode() const;", game)
        self.assertIn("class GameBalanceRuntime final", runtime)
        self.assertIn("friend class Game", runtime)
        self.assertIn("GameBalanceReadSnapshot", runtime)
        self.assertIn("CaptureAutomationSnapshot() const", runtime)
        self.assertIn("CaptureReadSnapshot(", runtime)
        self.assertIn("GameBalanceRuntime::CaptureReadSnapshot(", runtime_source)
        for external_boundary in (debug, mcp):
            self.assertNotIn("m_BalanceRuntime->autoPlayer", external_boundary)
            self.assertNotIn("m_BalanceRuntime->validation", external_boundary)
            self.assertNotIn("m_BalanceRuntime->scaling", external_boundary)

        self.assertIn("CaptureAutomationSnapshot()", debug)
        self.assertIn("CaptureReadSnapshot(", mcp)
        self.assertIn("SetAutoPlayEnabled(false)", mcp)
        self.assertIn("HasValidationVariant(variantId)", mcp)

    def test_non_battle_scenes_do_not_depend_on_game_header(self) -> None:
        for scene_name in (
            "RestSiteScene.cpp",
            "ShopScene.cpp",
            "TitleScene.cpp",
            "ResultScene.cpp",
            "StageSelectScene.cpp",
        ):
            scene = source(scene_name)
            self.assertNotIn('#include "Game.h"', scene)
            self.assertNotIn("Game::GetInstance()", scene)

        view = source("GameView.h")
        self.assertIn("RestSiteViewSnapshot", view)
        self.assertIn("ShopViewSnapshot", view)
        self.assertIn("TitleViewSnapshot", view)
        self.assertNotIn('#include "Game.h"', view)

    def test_route_sources_share_one_validated_command(self) -> None:
        stage_select = source("StageSelectScene.cpp")
        autoplay = source("GameAutoPlay.cpp")
        mcp = source("GameMcpBridge.cpp")
        boundary = source("GameSceneAccess.cpp")

        self.assertIn("RunRouteCommands::Choose(", stage_select)
        self.assertIn('stageSelect->ChooseRoute(selectedRoute, "autoplay")', autoplay)
        self.assertIn('stageSelect->ChooseRoute(routeIndex, "mcp")', mcp)
        self.assertIn("bool RunRouteCommands::Choose(", boundary)
        self.assertIn("game.ChooseMapNode(nodeId)", boundary)
        self.assertIn('game.RecordBalanceEvent(\n        "route_choice"', boundary)
        self.assertIn("game.StartNextBattle(StageType::Normal)", boundary)
        self.assertIn("game.ChangeScene(SceneType::Shop)", boundary)

    def test_battle_scene_delegates_setup_creation_and_hot_reload(self) -> None:
        scene = source("BattleScene.cpp")
        setup = source("BattleSceneSetup.cpp")
        objects = source("BattleSceneObjectFactory.cpp")
        hot_reload = source("BattleDebugHotReload.cpp")
        game_bridge = source("GameBattleBridge.cpp")
        scene_manager = source("SceneManager.cpp")

        for dependency in (
            '#include "Game.h"',
            '#include "StageDataLoader.h"',
            '#include "BallFactory.h"',
            '#include "Input.h"',
        ):
            self.assertNotIn(dependency, scene)

        self.assertIn("m_Setup->Prepare()", scene)
        self.assertIn("m_ObjectFactory->CreateArena", scene)
        self.assertIn("m_ObjectFactory->CreateStageObjects", scene)
        self.assertIn("m_HotReload->Update()", scene)
        self.assertIn("BattleSceneSetupResult BattleSceneSetup::Prepare()", setup)
        self.assertIn("BattleSceneObjectFactory::CreateArena", objects)
        self.assertIn("BattleDebugHotReload::UpdateJsonHotReload()", hot_reload)
        for boundary in (setup, objects, hot_reload):
            self.assertNotIn('#include "Game.h"', boundary)
            self.assertIn('#include "BattleSceneServices.h"', boundary)
            self.assertNotIn("Game::GetInstance()", boundary)
        self.assertIn("class GameBattleSceneServices final", game_bridge)
        self.assertIn("void Game::InitializeBattleSceneFactory()", game_bridge)
        self.assertIn("SetBattleSceneFactory", game_bridge)
        self.assertNotIn('#include "BattleScene.h"', scene_manager)
        self.assertIn("m_BattleSceneFactory()", scene_manager)

    def test_game_header_hides_mutable_battle_runtime_details(self) -> None:
        game = source("Game.h")
        battle_state = source("GameBattleState.h") + source("GameBattleBridge.cpp")

        self.assertIn("std::unique_ptr<GameBattleState> m_BattleState", game)
        self.assertIn('#include "BattleTypes.h"', game)
        for implementation_dependency in (
            '#include "BattleController.h"',
            '#include "HeavyCollisionRules.h"',
            '#include "AnchorStackRules.h"',
        ):
            self.assertNotIn(implementation_dependency, game)

        for hidden_member in (
            "BattleController m_BattleController",
            "HeavyCollisionRules::State m_HeavyCollisions",
            "AnchorStackRules::State m_AnchorStacks",
            "std::unordered_set<const EnemyBall*> m_PiercedEnemiesThisShot",
        ):
            self.assertNotIn(hidden_member, game)

        self.assertIn("BattleController controller", battle_state)
        self.assertIn("HeavyCollisionRules::State heavyCollisions", battle_state)
        self.assertIn("AnchorStackRules::State anchorStacks", battle_state)
        self.assertIn("BattleState Game::GetBattleState() const", battle_state)
        self.assertIn("bool Game::AreAllBallsStopped() const", battle_state)

    def test_game_header_hides_scene_and_physics_runtime_state(self) -> None:
        game = source("Game.h")
        runtime = source("GameRuntimeState.h") + source("GameRuntimeState.cpp")

        self.assertIn("std::unique_ptr<GameRuntimeState> m_RuntimeState", game)
        self.assertNotIn('#include "SceneManager.h"', game)
        self.assertNotIn('#include "FixedStepClock.h"', game)
        self.assertNotIn('#include "GameWorld.h"', game)
        self.assertNotIn("SceneManager m_SceneManager", game)
        self.assertNotIn("FixedStepClock m_PhysicsClock", game)
        self.assertNotIn("GameWorld m_World", game)
        self.assertNotIn("GetComponents", game)

        self.assertIn("GameWorld m_World", runtime)
        self.assertIn("SceneManager m_Scenes", runtime)
        self.assertIn("FixedStepClock m_PhysicsClock", runtime)
        self.assertIn("BeginPhysicsFrame", runtime)
        self.assertIn("CapturePhysicsTiming", runtime)

    def test_game_header_hides_settings_storage(self) -> None:
        game = source("Game.h")
        runtime = source("GameSettingsRuntime.h") + source("GameSettingsRuntime.cpp")

        self.assertIn("std::unique_ptr<GameSettingsRuntime> m_SettingsRuntime", game)
        self.assertNotIn('#include "SettingsManager.h"', game)
        self.assertNotIn("SettingsManager m_SettingsManager", game)
        self.assertIn("SettingsManager m_Manager", runtime)
        self.assertIn("void GameSettingsRuntime::ToggleFullscreen()", runtime)
        self.assertIn("bool GameSettingsRuntime::Replace", runtime)

    def test_game_header_hides_persistence_storage(self) -> None:
        game = source("Game.h")
        runtime = (
            source("GamePersistenceRuntime.h")
            + source("GamePersistenceRuntime.cpp")
        )

        self.assertIn(
            "std::unique_ptr<GamePersistenceRuntime> m_PersistenceRuntime",
            game,
        )
        self.assertNotIn('#include "ProgressionProfile.h"', game)
        self.assertNotIn('#include "RunStatisticsTracker.h"', game)
        self.assertNotIn("RunStatisticsTracker m_RunStatistics", game)
        self.assertNotIn("ProgressionProfile m_ProgressionProfile", game)
        self.assertIn("RunStatisticsTracker m_Statistics", runtime)
        self.assertIn("ProgressionProfile m_Profile", runtime)
        self.assertIn("void GamePersistenceRuntime::AdvanceFrame", runtime)

    def test_game_header_forward_declares_shot_relic_snapshot(self) -> None:
        game = source("Game.h")
        progression = source("GameProgression.cpp")
        battle_bridge = source("GameBattleBridge.cpp")

        self.assertIn("struct ShotRelicRules;", game)
        self.assertNotIn('#include "ShotRelicRules.h"', game)
        self.assertIn('#include "ShotRelicRules.h"', progression)
        self.assertIn('#include "ShotRelicRules.h"', battle_bridge)

    def test_game_header_hides_run_default_value_types(self) -> None:
        game = source("Game.h")
        defaults = source("GameRunDefaults.h") + source("GameRunDefaults.cpp")

        self.assertIn("std::unique_ptr<GameRunDefaults> m_RunDefaults", game)
        self.assertNotIn('#include "BallStatus.h"', game)
        self.assertNotIn('#include "PlayerRunStatus.h"', game)
        self.assertNotIn("BallStatus m_DefaultPlayerStatus", game)
        self.assertNotIn("PlayerRunStatus m_DefaultPlayerRunStatus", game)
        self.assertIn("BallStatus m_PlayerBallStatus", defaults)
        self.assertIn("PlayerRunStatus m_PlayerStatus", defaults)
        self.assertIn("float GameRunDefaults::PlayerRadius() const", defaults)

    def test_game_header_hides_run_session_storage(self) -> None:
        game = source("Game.h")
        session = source("GameRunSession.h") + source("GameRunSession.cpp")

        self.assertIn("std::unique_ptr<GameRunSession> m_RunSession", game)
        self.assertNotIn('#include "RunResultSnapshot.h"', game)
        self.assertNotIn("bool m_RunActive", game)
        self.assertNotIn("RunResultSnapshot m_LastRunResult", game)
        self.assertIn("bool m_Active", session)
        self.assertIn("RunResultSnapshot m_LastResult", session)
        self.assertIn("void GameRunSession::Complete", session)

    def test_game_header_hides_clear_reward_flow_state(self) -> None:
        game = source("Game.h")
        reward = source("GameRewardState.h") + source("GameRewardState.cpp")
        mcp = source("GameMcpBridge.cpp")

        self.assertIn("std::unique_ptr<GameRewardState> m_RewardState", game)
        for hidden_member in (
            "m_IsClearRewardActive",
            "m_SelectedRewardIndex",
            "m_SelectedRewardBallIndex",
            "m_ClearRewardBallOfferCatalogIndices",
            "m_RewardMessage",
            "m_IsClearRewardChosen",
            "m_ClearRewardMouseConfirmed",
            "m_IsMidBossRelicSelectionActive",
            "m_SelectedRelicOfferIndex",
        ):
            self.assertNotIn(hidden_member, game)
        self.assertIn("class GameRewardState final", reward)
        self.assertIn("void GameRewardState::Prepare", reward)
        self.assertIn("void GameRewardState::MarkChosen", reward)
        self.assertIn("bool GameRewardState::ConsumeConfirmationRequest", reward)
        self.assertNotIn("game.m_IsClearRewardActive", mcp)
        self.assertIn("game.IsClearRewardActive()", mcp)

    def test_game_header_hides_encounter_threat_configuration(self) -> None:
        game = source("Game.h")
        encounter = (
            source("GameEncounterBalance.h")
            + source("GameEncounterBalance.cpp")
        )
        config = source("GameBalanceConfig.cpp")
        autoplay = source("GameAutoPlay.cpp")

        self.assertIn(
            "std::unique_ptr<GameEncounterBalance> m_EncounterBalance",
            game,
        )
        self.assertNotIn('#include <unordered_map>', game)
        for hidden_member in (
            "m_EnemyThreatCosts",
            "m_StageThreatTargets",
            "m_StageDataLayoutThreatMultiplier",
            "m_DenseLayoutThreatMultiplier",
            "m_McpLayoutThreatMultiplier",
        ):
            self.assertNotIn(hidden_member, game)
        self.assertIn("EncounterThreatSnapshot", encounter)
        self.assertIn("void GameEncounterBalance::Load", encounter)
        self.assertIn("GameEncounterBalance::Evaluate", encounter)
        self.assertIn("m_EncounterBalance->Load(filePath)", config)
        self.assertIn("m_EncounterBalance->Evaluate", autoplay)

    def test_ball_offer_selection_uses_shared_validated_state(self) -> None:
        game = source("Game.h")
        state = (
            source("GameBallSelectionState.h")
            + source("GameBallSelectionState.cpp")
        )
        core = source("Game.cpp")
        autoplay = source("GameAutoPlay.cpp")
        boss = source("GameBossAI.cpp")
        mcp = source("GameMcpBridge.cpp")

        self.assertIn(
            "std::unique_ptr<GameBallSelectionState> m_BallSelectionState",
            game,
        )
        self.assertNotIn("int m_SelectedOfferIndex", game)
        self.assertNotIn("int m_SelectedHoldIndex", game)
        self.assertIn("bool GameBallSelectionState::SelectOffer", state)
        self.assertIn("bool GameBallSelectionState::ToggleHold", state)
        self.assertIn("m_BallSelectionState->Begin", core)
        self.assertIn("m_BallSelectionState->SelectOffer", autoplay)
        self.assertIn("ApplyBalanceAutoBallSelection(offerIndex)", boss)
        self.assertNotIn("game.m_SelectedOfferIndex", mcp)
        self.assertIn("game.GetSelectedBallOfferIndex()", mcp)

    def test_game_header_hides_pause_ui_state_and_requests(self) -> None:
        game = source("Game.h")
        pause = source("GamePauseState.h") + source("GamePauseState.cpp")
        core = source("Game.cpp")
        presentation = source("GamePresentation.cpp")

        self.assertIn("std::unique_ptr<GamePauseState> m_PauseState", game)
        for hidden_member in (
            "m_IsPaused",
            "m_MousePauseToggle",
            "m_MouseSaveRequested",
            "m_MouseFullscreenToggle",
            "m_PauseConfirmTitle",
            "m_PendingDisplayApply",
        ):
            self.assertNotIn(hidden_member, game)
        self.assertIn("class GamePauseState final", pause)
        self.assertIn("bool GamePauseState::TogglePaused", pause)
        self.assertIn("ConsumePauseToggleRequest", core)
        self.assertIn("ConsumeManualSaveRequest", core)
        self.assertIn("ConsumeFullscreenToggleRequest", core)
        self.assertIn("RequestReturnToTitleConfirmation", presentation)
        self.assertIn("ResumeAndDismissConfirmation", presentation)

    def test_game_header_hides_run_controller_implementation(self) -> None:
        game = source("Game.h")
        access = source("GameRunAccess.cpp")

        self.assertNotIn('#include "RunController.h"', game)
        self.assertNotIn('#include "RunProgressConstants.h"', game)
        self.assertNotIn('#include "PlayerDeck.h"', game)
        self.assertNotIn("class PlayerDeck;", game)
        self.assertNotIn("GetPlayerDeckView", game)
        self.assertIn("std::unique_ptr<RunController> m_RunController", game)
        self.assertNotIn("m_RunController.", game)
        self.assertIn("int Game::GetPlayerDeckCount() const", access)
        self.assertIn("Game::GetDrawPileBall", access)
        self.assertIn("Game::GetDiscardPileBall", access)
        self.assertIn("Game::IsCurrentPlayerBallUsed", access)
        self.assertIn("int Game::GetPlayerMoney() const", access)
        self.assertIn("bool Game::HasRelic(RelicType type) const", access)
        self.assertIn("int Game::GetShopRelicOfferCount() const", access)
        self.assertIn("int Game::GetMidBossRelicOfferCount() const", access)

        for consumer_name in (
            "GameAutoPlay.cpp",
            "GameMcpBridge.cpp",
            "GamePresentation.cpp",
        ):
            consumer = source(consumer_name)
            self.assertNotIn('#include "PlayerDeck.h"', consumer)
            self.assertNotIn("GetPlayerDeckView", consumer)
            self.assertNotIn("m_RunController->Deck()", consumer)

    def test_game_header_hides_external_stage_override_storage(self) -> None:
        game = source("Game.h")
        state = (
            source("GameStageOverrideState.h")
            + source("GameStageOverrideState.cpp")
        )
        core = source("Game.cpp")
        mcp = source("GameMcpBridge.cpp")
        debug = source("GameDebugMode.cpp")

        self.assertNotIn('#include "StageData.h"', game)
        self.assertIn("std::unique_ptr<GameStageOverrideState>", game)
        self.assertNotIn("m_McpNextStageOverride", game)
        self.assertNotIn("m_McpCurrentStageOverride", game)
        self.assertIn("StageData* GameStageOverrideState::PromoteQueuedToCurrent", state)
        self.assertIn("m_StageOverrideState->PromoteQueuedToCurrent()", core)
        self.assertIn("m_StageOverrideState->Queue", mcp)
        self.assertIn("m_StageOverrideState->Queue", debug)

    def test_stage_and_table_render_use_narrow_battle_boundaries(self) -> None:
        stage = source("StageBase.cpp")
        table_render = source("TableFrameRenderComponent.cpp")
        bridge = source("GameBattleBridge.cpp")

        for consumer in (stage, table_render):
            self.assertNotIn('#include "Game.h"', consumer)
            self.assertNotIn("Game::GetInstance()", consumer)

        self.assertIn("GameBattleView::State()", stage)
        self.assertIn("GameBattleCommands::CompleteCurrentStage()", stage)
        self.assertIn("GameRuntime::World()", stage)
        self.assertIn("GameRuntime::DestroyObject", stage)

        self.assertIn("GameRuntime::MainCamera()", table_render)
        self.assertIn("GameBattleView::IsCushionChargeActive", table_render)
        self.assertIn("BattleState GameBattleView::State()", bridge)
        self.assertIn("void GameBattleCommands::CompleteCurrentStage()", bridge)

    def test_ball_collision_emits_facts_through_battle_boundaries(self) -> None:
        collision = source("BallCollisionComponent.cpp")
        events = source("GameBattleEvents.h") + source("GameBattleBridge.cpp")

        self.assertNotIn('#include "Game.h"', collision)
        self.assertNotIn("Game::GetInstance()", collision)
        self.assertIn("GameRuntime::World().GetComponents", collision)
        self.assertIn("GameBattleView::State()", collision)
        self.assertIn("GameBattleEvents::PlayerWallCollision", collision)
        self.assertIn("GameBattleEvents::EnemyEnemySynergyCollision", collision)
        self.assertIn("GameBattleEvents::PlayerEnemySynergyCollision", collision)
        self.assertIn("GameBattleEvents::CombatFeedback", collision)
        self.assertIn("LiveShotRules liveShotRules", collision)

        self.assertIn("class GameBattleEvents final", events)
        self.assertIn("void GameBattleEvents::PlayerWallCollision", events)
        self.assertIn("void GameBattleEvents::DamageBallCollision", events)

    def test_game_header_does_not_restore_obsolete_world_accessors(self) -> None:
        game = source("Game.h")
        core = source("Game.cpp")
        battle = source("GameBattleBridge.cpp")

        for obsolete_api in (
            "GetCamera(",
            "GetComponents<",
            "GetGameObjectsWithTag(",
            "ContainsGameObject(",
            "GetPierceTraceCount(",
            "GetTotalCushionStacks(",
        ):
            self.assertNotIn(obsolete_api, game)

        self.assertNotIn("Game::ContainsGameObject(", core)
        self.assertNotIn("Game::GetPierceTraceCount(", battle)
        self.assertNotIn("Game::GetTotalCushionStacks(", battle)


if __name__ == "__main__":
    unittest.main()
