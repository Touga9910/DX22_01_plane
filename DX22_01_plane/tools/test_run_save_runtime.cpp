#include "../GameSaveManager.h"
#include "../GameRunSaveState.h"
#include "../GamePersistenceRuntime.h"
#include "../json/json.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <string_view>
#include "test_checks.h"

namespace fs = std::filesystem;
using nlohmann::json;
const fs::path path = "saves/run_save.json";

/// <summary>
/// テスト用のセーブファイルをバイナリで読み込み、その内容を返す。
/// </summary>
std::string Read()
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
/// <summary>
/// テスト用のセーブファイルを指定されたバイト列で上書きする。
/// </summary>
void Write(const std::string& bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << bytes;
    assert(file.good());
}

/// <summary>
/// 隔離された作業場所で、セーブの保存と復元、不正データの拒否、失敗時の保護を検証する。
/// </summary>
int main(int argc, char** argv)
{
    // Never run this fixture against a user's existing saves.
    assert(!fs::exists("saves"));
    GameRunSaveSnapshot snapshot;
    snapshot.status.maxHp = 40;
    snapshot.status.currentHp = 31;
    snapshot.status.money = 27;
    snapshot.status.progress = 1;
    snapshot.progress.map.Generate(20261120);
    snapshot.runSeed = 20261120;
    snapshot.routeSelectionSeed = 20261120;
    snapshot.stageSelectionSeed = 1024;
    snapshot.deck.randomEngine.seed(12345);
    snapshot.pocketRandomEngine.seed(54321);
    snapshot.relicRandomEngine.seed(91723);
    for (int i = 0; i < 5; ++i)
    {
        PlayerBallData ball;
        ball.definitionId = "player_standard";
        ball.instanceId = i + 1;
        snapshot.deck.drawPile.push_back(ball);
    }
    snapshot.deck.nextInstanceId = 6;
    GameRunLoadRules rules{false, 15, static_cast<int>(RelicType::Count), 18};
    GameRunRestoreRequest restored;
    std::string message;
    assert(!GameSaveManager::Inspect().exists);
    assert(!GameSaveManager::Load(rules, restored, message));
    assert(GameSaveManager::Save(snapshot, SceneType::Select, false, false, message));
    const std::string original = Read();
    const auto info = GameSaveManager::Inspect();
    assert(info.exists && info.valid && info.currentHp == 31 && info.money == 27);
    assert(GameSaveManager::Load(rules, restored, message));
    assert(restored.resumeScene == SceneType::Select);
    assert(restored.snapshot.status.currentHp == 31 && restored.snapshot.status.maxHp == 40);
    assert(restored.snapshot.status.money == 27);
    assert(restored.snapshot.deck.drawPile.size() == 5);
    assert(restored.snapshot.deck.nextInstanceId == 6);
    assert(restored.snapshot.deck.randomEngine == snapshot.deck.randomEngine);
    assert(restored.snapshot.pocketRandomEngine == snapshot.pocketRandomEngine);
    assert(restored.snapshot.relicRandomEngine == snapshot.relicRandomEngine);
    assert(restored.snapshot.progress.map.Save() == snapshot.progress.map.Save());
    assert(!GameSaveManager::Save(snapshot, SceneType::Title, false, false, message));
    assert(!GameSaveManager::Save(snapshot, SceneType::Select, true, false, message));
    assert(Read() == original);

    // A failed temporary write must preserve the existing valid checkpoint.
    fs::create_directory(path.string() + ".tmp");
    assert(!GameSaveManager::Save(snapshot, SceneType::Select, false, false, message));
    assert(Read() == original);
    fs::remove(path.string() + ".tmp");
    Write("{truncated");
    assert(!GameSaveManager::Inspect().valid);
    assert(!GameSaveManager::Load(rules, restored, message));
    auto document = json::parse(original);
    document["payload"]["run"]["money"] = 999;
    Write(document.dump());
    assert(!GameSaveManager::Load(rules, restored, message));
    document = json::parse(original);
    document["schema_version"] = 999;
    Write(document.dump());
    assert(!GameSaveManager::Load(rules, restored, message));
    Write(original);
    assert(GameSaveManager::Load(rules, restored, message));

    // Interrupting a battle must restart the entire encounter from its entry
    // checkpoint, including HP, shot statistics, ball order and random state.
    // The enemy arena is rebuilt too, so saving the live player state would
    // retain damage while reviving every enemy and count the battle twice.
    auto battleEntry = snapshot;
    assert(battleEntry.progress.map.Choose(battleEntry.progress.map.Available().front()));
    battleEntry.status.SetSelectedStageId("normal_checkpoint_fixture");
    GamePersistenceRuntime persistence;
    assert(persistence.SaveCheckpoint(battleEntry, SceneType::Battle, false, false, message));
    PlayerDeck liveDeck;
    assert(liveDeck.RestoreSnapshot(battleEntry.deck));
    liveDeck.Reset();
    assert(liveDeck.PrepareOffer(3));
    std::vector<std::uint64_t> firstOffers;
    for (int index = 0; index < 3; ++index) firstOffers.push_back(liveDeck.GetOffer(index)->instanceId);
    assert(liveDeck.SelectOffer(0));
    RunStatisticsTracker battleStatistics;
    battleStatistics.Restore(battleEntry.statistics);
    battleStatistics.BeginBattle(false, false, battleEntry.status.GetSelectedStageId());
    battleStatistics.BeginShot("player_standard");
    battleStatistics.RecordPlayerDamage(7);
    auto interruptedBattle = battleEntry;
    interruptedBattle.status.currentHp -= 7;
    interruptedBattle.statistics = battleStatistics.GetState();
    interruptedBattle.deck = liveDeck.CaptureSnapshot();
    interruptedBattle.pocketRandomEngine();
    assert(persistence.SaveCheckpoint(interruptedBattle, SceneType::Battle, true, false, message));
    assert(GameSaveManager::Load(rules, restored, message));
    assert(restored.snapshot.status.currentHp == battleEntry.status.currentHp);
    assert(restored.snapshot.statistics.totalBattles == 0 && restored.snapshot.statistics.totalShots == 0);
    assert(restored.snapshot.statistics.damageTaken == 0);
    assert(restored.snapshot.deck.randomEngine == battleEntry.deck.randomEngine);
    assert(restored.snapshot.pocketRandomEngine == battleEntry.pocketRandomEngine);
    PlayerDeck restartedDeck;
    assert(restartedDeck.RestoreSnapshot(restored.snapshot.deck));
    restartedDeck.Reset();
    assert(restartedDeck.PrepareOffer(3));
    for (int index = 0; index < 3; ++index) assert(restartedDeck.GetOffer(index)->instanceId == firstOffers[index]);
    battleStatistics.Restore(restored.snapshot.statistics);
    battleStatistics.BeginBattle(false, false, battleEntry.status.GetSelectedStageId());
    assert(battleStatistics.GetState().totalBattles == 1);

    // After loading, another interruption uses the loaded entry checkpoint.
    // Clearing a run or abandoning a failed load must reject stale state.
    persistence.ClearBattleCheckpoint();
    const std::string battleCheckpointBytes = Read();
    assert(!persistence.SaveCheckpoint(interruptedBattle, SceneType::Battle, true, false, message));
    assert(Read() == battleCheckpointBytes);
    persistence.RestoreBattleCheckpoint(restored.snapshot);
    assert(persistence.SaveCheckpoint(interruptedBattle, SceneType::Battle, true, false, message));
    assert(GameSaveManager::Load(rules, restored, message));
    assert(restored.snapshot.statistics.totalBattles == 0 && restored.snapshot.status.currentHp == 31);

    // A phase encounter restarts at its original map, stage and encounter
    // ordinal, even if interruption occurs after an internal battle phase.
    auto phaseEntry = battleEntry;
    phaseEntry.runCombatMode = RunCombatMode::PhaseTest;
    phaseEntry.phaseProgressionMode = PhaseRunProgressionMode::RouteProgress;
    phaseEntry.progress.phaseNormalBattleEncounters = 3;
    phaseEntry.status.SetSelectedStageId("phase_checkpoint_fixture");
    assert(persistence.SaveCheckpoint(phaseEntry, SceneType::Battle, false, false, message));
    auto interruptedPhase = phaseEntry;
    interruptedPhase.status.currentHp = 1;
    interruptedPhase.statistics.totalBattles = 1;
    assert(persistence.SaveCheckpoint(interruptedPhase, SceneType::Battle, true, false, message));
    assert(GameSaveManager::Load(rules, restored, message));
    assert(restored.snapshot.runCombatMode == RunCombatMode::PhaseTest);
    assert(restored.snapshot.phaseProgressionMode == PhaseRunProgressionMode::RouteProgress);
    assert(restored.snapshot.progress.map.Save() == phaseEntry.progress.map.Save());
    assert(restored.snapshot.status.GetSelectedStageId() == phaseEntry.status.GetSelectedStageId());
    assert(restored.snapshot.progress.phaseNormalBattleEncounters == 3);
    assert(restored.snapshot.status.currentHp == phaseEntry.status.currentHp);
    assert(restored.snapshot.statistics.totalBattles == 0);

    assert(persistence.SaveCheckpoint(snapshot, SceneType::Select, true, false, message));
    const std::string routeCheckpointBytes = Read();
    assert(!persistence.SaveCheckpoint(interruptedPhase, SceneType::Battle, true, false, message));
    assert(Read() == routeCheckpointBytes);

    // The last ordinary battle advances the map to boss preparation before
    // its reward UI closes. Pausing after choosing the bonus must therefore
    // resume at the preparation rest, rather than a route-selection screen.
    RunProgressController endpointProgress;
    endpointProgress.Reset(snapshot.routeSelectionSeed);
    for (int area = 0; area < 15; ++area)
    {
        assert(endpointProgress.ChooseNode(endpointProgress.GetMap().Available().front()));
        const auto completion = endpointProgress.CompleteNormalArea(false, 15, 0);
        assert(completion == (area == 14 ? AreaCompletionResult::BossPreparationEntered : AreaCompletionResult::Advanced));
    }
    auto endpointReward = snapshot;
    endpointReward.progress = endpointProgress.Capture();
    endpointReward.status.progress = 15;
    endpointReward.statistics.areaProgress = 15;
    assert(GameSaveManager::Save(endpointReward, SceneType::Select, false, false, message));
    assert(!GameSaveManager::Load(rules, restored, message));
    assert(persistence.SaveCheckpoint(endpointReward, SceneType::RestSite, false, false, message));
    assert(GameSaveManager::Load(rules, restored, message));
    assert(restored.resumeScene == SceneType::RestSite && !restored.restActionUsed);
    assert(restored.snapshot.progress.phase == RunPhase::BossPreparation);
    assert(restored.snapshot.progress.map.Save() == endpointReward.progress.map.Save());

    // Park a route at an event without starting a battle, then reload its
    // accumulated upgrade count and continue on the same map.
    auto eventRun = snapshot;
    auto& map = eventRun.progress.map;
    int eventNode = -1;
    while (map.CompletedAreas() < map.AreaCount())
    {
        const auto available = map.Available();
        assert(!available.empty());
        for (int id : available)
        {
            if (map.Node(id)->type == StageRouteType::Event)
            {
                eventNode = id;
                break;
            }
        }
        assert(map.Choose(eventNode >= 0 ? eventNode : available.front()));
        if (eventNode >= 0) break;
        assert(map.CompleteActive());
    }
    assert(eventNode >= 0);
    eventRun.progress.areaProgress = map.CompletedAreas();
    eventRun.runEventActions = 2;
    eventRun.runEventId = "polishing_table";
    eventRun.status.currentHp = 18;
    eventRun.deck.drawPile.front().upgradeLevel = 2;
    eventRun.runEventResult = {true, true, "polishing_table", "球を強化しました", "スタンダード球 +1 → +2", 26, 18, 27, 27};
    assert(GameSaveManager::Save(eventRun, SceneType::Event, false, false, message));
    assert(GameSaveManager::Load(rules, restored, message));
    assert(restored.resumeScene == SceneType::Event);
    assert(restored.snapshot.runEventActions == 2);
    assert(restored.snapshot.runEventId == "polishing_table");
    assert(restored.snapshot.runEventResult.pending && restored.snapshot.runEventResult.repeatable);
    assert(restored.snapshot.runEventResult.detail == eventRun.runEventResult.detail);
    assert(restored.snapshot.runEventResult.hpBefore == 26 && restored.snapshot.runEventResult.hpAfter == 18);
    // Reject a result that does not describe the already-applied saved state.
    auto inconsistentEvent = eventRun;
    inconsistentEvent.runEventResult.hpAfter = 19;
    assert(GameSaveManager::Save(inconsistentEvent, SceneType::Event, false, false, message));
    assert(!GameSaveManager::Load(rules, restored, message));
    assert(GameSaveManager::Save(eventRun, SceneType::Event, false, false, message));
    assert(GameSaveManager::Load(rules, restored, message));
    assert(restored.snapshot.status.currentHp == 18);
    auto legacyPolish = eventRun;
    legacyPolish.runEventId.clear();
    legacyPolish.runEventResult = {};
    assert(GameSaveManager::Save(legacyPolish, SceneType::Event, false, false, message));
    assert(GameSaveManager::Load(rules, restored, message));
    assert(restored.snapshot.runEventId == "polishing_table" && restored.snapshot.runEventActions == 2);
    assert(GameSaveManager::Save(eventRun, SceneType::Event, false, false, message));
    assert(GameSaveManager::Load(rules, restored, message));
    assert(restored.snapshot.deck.drawPile.front().upgradeLevel == 2);
    assert(restored.snapshot.progress.map.Save() == map.Save());
    // Optional isolated checkpoints for exercising the real UI after this test.
    if (argc == 2 && std::string_view(argv[1]) == "--write-event-fixtures")
    {
        fs::copy_file(path, "event_result_fixture.json", fs::copy_options::overwrite_existing);
        auto choiceFixture = eventRun;
        choiceFixture.runEventId = "pendulum_safe";
        choiceFixture.runEventActions = 0;
        choiceFixture.runEventResult = {};
        assert(GameSaveManager::Save(choiceFixture, SceneType::Event, false, false, message));
        fs::copy_file(path, "event_choice_fixture.json", fs::copy_options::overwrite_existing);
    }
    assert(map.CompleteActive());
    eventRun.progress.areaProgress = map.CompletedAreas();
    assert(GameSaveManager::Save(eventRun, SceneType::Select, false, false, message));
    assert(GameSaveManager::Load(rules, restored, message));
    assert(restored.resumeScene == SceneType::Select);
    assert(restored.snapshot.runEventActions == 0);
    assert(restored.snapshot.runEventId.empty() && !restored.snapshot.runEventResult.pending);

    // A previous shop leaves its offer vectors populated on the route map.
    // Load this private fixture and enter the first node to verify that the
    // incoming shop autosaves its new offers and RNG after ShopScene::Init.
    if (argc == 2 && std::string_view(argv[1]) == "--write-shop-fixtures")
    {
        auto shopFixture = snapshot;
        shopFixture.progress.map.MigrateEntry(StageRouteType::Shop);
        shopFixture.progress.map.CancelActive();
        shopFixture.shopBallOffers = {0, 1, 2};
        shopFixture.shopRelicOffers = {0, 1, 2};
        assert(GameSaveManager::Save(shopFixture, SceneType::Select, false, false, message));
        assert(GameSaveManager::Load(rules, restored, message));
        assert(restored.snapshot.progress.map.Available().front() == 0);
        assert(restored.snapshot.progress.map.Node(0)->type == StageRouteType::Shop);
        fs::copy_file(path, "shop_entry_fixture.json", fs::copy_options::overwrite_existing);
    }

    assert(GameSaveManager::Remove());
    assert(!GameSaveManager::Inspect().exists);
    std::cout << "PASS: save/load, battle/phase restart, deck IDs, map, RNG, event progress, invalid/debug saves, atomic failure and recovery\n";
}
