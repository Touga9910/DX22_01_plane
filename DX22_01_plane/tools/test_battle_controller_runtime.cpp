#include "../BattleController.h"
#include "../BattleFlowRules.h"
#include "../GameObject.h"
#include "../GameWorld.h"
#include "../StageDataLoader.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

class BattleControllerTestAccess final
{
public:
    /// <summary>
    /// テスト用に敵の生存数を指定して、戦闘フェーズの進行を試みる。
    /// </summary>
    static bool AdvancePhase(
        BattleController& controller,
        int livingEnemyCount)
    {
        return controller.TryAdvancePhase(livingEnemyCount);
    }

    /// <summary>
    /// テスト用に敵の生存数を指定して、現在のフェーズで勝利が成立するかを判定する。
    /// </summary>
    static bool IsPhaseVictory(
        const BattleController& controller,
        int livingEnemyCount)
    {
        return controller.IsPhaseVictoryForLivingCount(livingEnemyCount);
    }

    /// <summary>
    /// 戦闘をショット移動中に設定し、停止判定の確定条件を満たすようにする。
    /// </summary>
    static void ForceMovingShotReady(BattleController& controller)
    {
        controller.m_State = BattleState::BallsMoving;
        controller.m_Active = true;
        controller.m_AllBallsStoppedTickCount =
            BattleFlowRules::RequiredStoppedTicks;
    }

    /// <summary>
    /// 戦闘を進行中の敵攻撃状態に設定する。
    /// </summary>
    static void ForceEnemyAttack(BattleController& controller)
    {
        controller.m_State = BattleState::EnemyAttack;
        controller.m_Active = true;
    }
};

namespace
{
    /// <summary>
    /// ステージ一覧から指定IDに一致するステージを探す。
    /// </summary>
    const StageData& FindStage(
        const std::vector<StageData>& stages,
        const std::string& id)
    {
        const auto found = std::find_if(
            stages.begin(), stages.end(),
            [&id](const StageData& stage) { return stage.id == id; });
        assert(found != stages.end());
        return *found;
    }

    struct SpawnObservation
    {
        int calls = 0;
        std::vector<int> enemyCounts;
        std::vector<int> phaseNumbers;
    };

    /// <summary>
    /// 敵の出現や自球の復帰を記録できるテスト用の戦闘コントローラーを作る。
    /// </summary>
    BattleController MakeController(
        GameWorld& world,
        SpawnObservation& spawns,
        int& restoredPlayerCount)
    {
        BattleController controller;
        BattleControllerHooks hooks;
        hooks.spawnPhaseEnemies =
            [&spawns](const std::vector<EnemySpawnData>& enemies,
                int, unsigned int, int phaseNumber)
            {
                ++spawns.calls;
                spawns.enemyCounts.push_back(
                    static_cast<int>(enemies.size()));
                spawns.phaseNumbers.push_back(phaseNumber);
            };
        hooks.restorePocketedPlayer = [&restoredPlayerCount]()
        {
            ++restoredPlayerCount;
        };
        hooks.prepareNextTurn = []() { return true; };
        controller.Initialize(world, std::move(hooks));
        return controller;
    }

    /// <summary>
    /// 旧戦闘モードでポケットによる撃破が勝利になり、勝利結果が一度だけ取得できることを検証する。
    /// </summary>
    void VerifyLegacyPocketClear()
    {
        GameWorld world;
        SpawnObservation spawns;
        int restoredPlayerCount = 0;
        auto controller = MakeController(world, spawns, restoredPlayerCount);
        StageData stage;
        stage.combatMode = CombatMode::Legacy;
        controller.BeginStage(stage);
        controller.StartBattle();
        assert(!controller.AreAllEnemiesDefeated()); // Empty while loading.
        // EnemyBall has emitted its once-only defeat; the pocket removed its object.
        controller.NotifyEnemyDefeated();
        controller.NotifyEnemyPocketDefeated();
        assert(controller.AreAllEnemiesDefeated());
        controller.Update();
        assert(controller.GetResult() == BattleResult::Victory);
        assert(controller.ConsumeResult() == BattleResult::Victory);
        assert(controller.ConsumeResult() == BattleResult::None);
    }

    /// <summary>
    /// 一つのフェーズだけで構成する戦闘が、敵の全滅時に次の出現処理を行わずに勝利することを検証する。
    /// </summary>
    void VerifySinglePhase(
        const StageData& stage)
    {
        assert(stage.phaseBattle.phases.size() == 1);
        assert(stage.phaseBattle.phases[0].enemies.size() == 6);

        GameWorld world;
        SpawnObservation spawns;
        int restoredPlayerCount = 0;
        BattleController controller = MakeController(
            world, spawns, restoredPlayerCount);
        controller.BeginStage(stage);
        controller.StartBattle();

        assert(controller.GetCurrentPhase() == 1);
        assert(controller.GetTotalPhases() == 1);
        assert(controller.GetNextPhaseEnemyCount() == 0);
        assert(!BattleControllerTestAccess::IsPhaseVictory(controller, 1));
        assert(BattleControllerTestAccess::IsPhaseVictory(controller, 0));
        assert(!BattleControllerTestAccess::AdvancePhase(controller, 0));
        assert(spawns.calls == 0);
    }

    /// <summary>
    /// 二つのフェーズを持つ戦闘で、所定の敵数が出現し、最終フェーズの全滅で勝利することを検証する。
    /// </summary>
    void VerifyTwoPhase(
        const StageData& stage,
        int expectedFirstPhaseCount,
        int expectedSecondPhaseCount)
    {
        assert(stage.phaseBattle.phases.size() == 2);
        assert(static_cast<int>(stage.phaseBattle.phases[0].enemies.size()) == expectedFirstPhaseCount);
        assert(static_cast<int>(stage.phaseBattle.phases[1].enemies.size()) ==
            expectedSecondPhaseCount);

        GameWorld world;
        SpawnObservation spawns;
        int restoredPlayerCount = 0;
        BattleController controller = MakeController(
            world, spawns, restoredPlayerCount);
        controller.BeginStage(stage);
        controller.StartBattle();

        assert(controller.GetCurrentPhase() == 1);
        assert(controller.GetTotalPhases() == 2);
        assert(controller.GetTurnsUntilNextPhase() ==
            stage.phaseBattle.turnsPerPhase);
        assert(controller.GetNextPhaseEnemyCount() ==
            expectedSecondPhaseCount);
        assert(!BattleControllerTestAccess::IsPhaseVictory(controller, 0));
		controller.NotifyEnemyDefeated();
		controller.NotifyEnemyDefeated();
		controller.NotifyEnemyPocketDefeated();
		assert(controller.GetDefeatedEnemyCount() == 2);
		assert(controller.GetPocketDefeatedEnemyCount() == 1);

        assert(!BattleControllerTestAccess::AdvancePhase(controller, expectedFirstPhaseCount));
        assert(spawns.calls == 0);

        assert(BattleControllerTestAccess::AdvancePhase(controller, 0));
        assert(spawns.calls == 1);
        assert(spawns.enemyCounts ==
            std::vector<int>({ expectedSecondPhaseCount }));
        assert(spawns.phaseNumbers == std::vector<int>({ 2 }));
        assert(controller.GetCurrentPhase() == 2);
        assert(controller.GetTurnsUntilNextPhase() == 0);
        assert(!BattleControllerTestAccess::IsPhaseVictory(controller, 1));
        assert(BattleControllerTestAccess::IsPhaseVictory(controller, 0));
        assert(!BattleControllerTestAccess::AdvancePhase(controller, 0));
        assert(spawns.calls == 1);
    }

    /// <summary>
    /// フェーズ途中の敗北判定と、重複更新で結果が変わらないことを検証する。
    /// </summary>
    void VerifyDefeatAndDuplicateGuards(
        const StageData& stage)
    {
        GameWorld world;
        SpawnObservation spawns;
        int restoredPlayerCount = 0;
        BattleController controller = MakeController(
            world, spawns, restoredPlayerCount);
        controller.BeginStage(stage);
        controller.StartBattle();

        controller.NotifyPlayerDefeated();
        const std::size_t defeatHistorySize =
            controller.GetStateHistory().size();
        controller.NotifyPlayerDefeated();
        controller.Update();
        assert(controller.GetResult() == BattleResult::Defeat);
        assert(controller.GetStateHistory().size() == defeatHistorySize);
        assert(controller.ConsumeResult() == BattleResult::Defeat);
        assert(controller.ConsumeResult() == BattleResult::None);

        BattleController shotController = MakeController(
            world, spawns, restoredPlayerCount);
        shotController.BeginStage(stage);
        shotController.StartBattle();
        BattleControllerTestAccess::ForceMovingShotReady(shotController);
        shotController.Update();
        const std::size_t shotHistorySize =
            shotController.GetStateHistory().size();
        assert(shotController.IsFinished());
        assert(shotController.GetResult() == BattleResult::Defeat);
        assert(restoredPlayerCount == 1);
        shotController.Update();
        assert(restoredPlayerCount == 1);
        assert(shotController.GetStateHistory().size() == shotHistorySize);

        BattleController attackController = MakeController(
            world, spawns, restoredPlayerCount);
        attackController.BeginStage(stage);
        attackController.StartBattle();
        BattleControllerTestAccess::ForceEnemyAttack(attackController);
        attackController.Update();
        const std::size_t attackHistorySize =
            attackController.GetStateHistory().size();
        assert(attackController.IsFinished());
        assert(attackController.GetResult() == BattleResult::Defeat);
        attackController.Update();
        assert(attackController.GetStateHistory().size() == attackHistorySize);
    }
}

/// <summary>
/// ステージデータを読み込み、戦闘コントローラーのフェーズ進行と勝敗判定を検証する。
/// </summary>
int main()
{
    const std::vector<StageData> stages = StageDataLoader::LoadAll(
        "assets/data/stage_01.json",
        "assets/data/enemy_data.json");

    const StageData& opening = FindStage(stages, "normal_001");
    const StageData& boss = FindStage(stages, "boss_001");
    StageData single = FindStage(stages, "campaign_phase_mid_mixed");
    single.phaseBattle.phases.resize(1);
    const StageData& sixToFour = FindStage(
		stages, "campaign_phase_mid_mixed");
    const StageData& sixToSix = FindStage(
		stages, "campaign_phase_late_mixed");

    VerifyLegacyPocketClear();
    VerifySinglePhase(single);
    VerifyTwoPhase(opening, 3, 3);
    VerifyTwoPhase(boss, 1, 2);
    VerifyTwoPhase(sixToFour, 6, 4);
    VerifyTwoPhase(sixToSix, 6, 6);
    VerifyDefeatAndDuplicateGuards(sixToSix);

    std::cout <<
        "BattleController runtime: 6x1, 3->3, boss, 6->4, 6->6, final victory, "
        "mid-phase defeat, and duplicate update guards passed\n";
}
