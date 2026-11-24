#include "DebugBattleSetup.h"
#include "PlayerDeck.h"
#include "DebugRunSetup.h"
#include "DebugRunPreset.h"
#include "RunController.h"
#include <cassert>
#include <fstream>
#include <limits>
#include <iostream>

/// <summary>
/// デバッグ戦闘のステージ指定、ルート遷移、プリセット検証と保存をテストする。
/// </summary>
int main()
{
    // Jumps keep a connected remaining map, including both final boss transitions.
    for (int area = 0; area < kNormalRouteAreaGoal; ++area)
    {
        for (int destination = 0; destination <= 6; ++destination)
        {
            DebugRunSetup run;
            run.completedAreas = area;
            run.destination = static_cast<DebugRunDestination>(destination);
            auto state = run.BuildProgress(12345);
            assert(state.areaProgress == (destination >= 5 ? 15 : area));
            assert(state.map.CompletedAreas() == state.areaProgress);
            assert(RunMap::Restore(state.map.Save()).Save() == state.map.Save());
            RunProgressController progress;
            progress.Restore(state);
            if (destination >= 5)
            {
                if (destination == 5) assert(progress.CompleteBossPreparation());
                assert(progress.BeginFinalBoss());
                progress.CompleteFinalBoss();
                assert(progress.GetPhase() == RunPhase::Completed);
            }
            else
            {
                if (destination == 0) assert(progress.ChooseNode(progress.GetMap().Available().front()));
                assert(progress.CompleteNormalArea(false, 15, 0) != AreaCompletionResult::Rejected);
                assert(progress.GetAreaProgress() == area + 1);
                if (area == 14) assert(progress.IsBossPreparation());
            }
        }
    }
    RunController economy;
    economy.Status().money = 10;
    assert(!economy.SpendMoney(20));
    economy.SetDebugInfiniteMoney(true);
    assert(economy.Status().money == 999999);
    assert(economy.SpendMoney(20) && economy.Status().money == 999999);
    economy.AddMoney(200);
    assert(economy.Status().money == 999999);
    economy.SetDebugInfiniteMoney(false);
    assert(economy.SpendMoney(20) && economy.Status().money == 999979);
    economy.ResetRuntimeState({});
    economy.Status().money = 10;
    assert(!economy.SpendMoney(20));
    const auto catalog = PlayerBallDataLoader::Load("assets/data/player_status.json", {}, {}).ballDefinitions;
    economy.Deck().SetCatalog(catalog);
    economy.Deck().SetDefaultDeck(std::vector<PlayerBallData>(5, catalog.front()));
    economy.Deck().ResetToDefault();
    economy.SetDebugInfiniteMoney(true);
    assert(economy.BuyShopBall(0, 20) && economy.Status().money == 999999);
    assert(economy.Deck().GetRewardTargetCount() == 6);
    assert(economy.RemoveShopBall(0, 15) && economy.Status().money == 999999);
    assert(economy.Deck().GetRewardTargetCount() == 5);
    assert(economy.BuyRelic(0) && economy.Status().money == 999999);
    assert(!economy.BuyRelic(0)); // Infinite money does not bypass ownership/offer rules.
    const auto enemies = StageDataLoader::LoadEnemyDefinitions("assets/data/enemy_data.json");
    auto ball = [&](const std::string& id) { return *std::find_if(catalog.begin(), catalog.end(), [&](const auto& b) { return b.definitionId == id; }); };
    auto foe = [&](const std::string& id) {
        DebugBattleSetup::Enemy e;
        e.spawn.enemyData = *std::find_if(enemies.begin(), enemies.end(), [&](const auto& b) { return b.id == id; });
        e.spawn.enemyId = id; e.spawn.position = {20, 1, 0}; e.hp = e.spawn.enemyData.maxHp;
        return e;
    };
    DebugBattleSetup setup;
    setup.deck = {ball("player_standard"), ball("player_heavy"), ball("player_heavy")};
    setup.deck[1].upgradeLevel = 2; setup.deck[1].status = setup.deck[1].upgradeTable[1];
    setup.enemies = {foe("enemy_normal")};
    setup.hp = 13; setup.money = 321; setup.relics[0] = true;
    setup.highestUnlockedAscension = 7;
    setup.selectedAscension = 5;
    setup.achievements[static_cast<size_t>(AchievementId::FirstVictory)] = true;
    setup.achievements[static_cast<size_t>(AchievementId::DamageEight)] = true;
    setup.breakBallPositions = {{-12, TableConfig::FIELD_HEIGHT, 8},
        {11, TableConfig::FIELD_HEIGHT, -7}, {0, TableConfig::FIELD_HEIGHT, 15}};
    setup.enemyValidation.enabled = true;
    setup.enemyValidation.hpMultiplier = 1.5f;
    setup.enemyValidation.enemyCount = 10;
    setup.enemyValidation.layoutPattern = EnemyValidationLayoutPattern::Line;
    assert(setup.Validate().empty());
    const auto roundtrip = DebugBattleSetup::FromJson(setup.ToJson(), catalog, enemies);
    assert(roundtrip.ToJson() == setup.ToJson());
    const auto stages = StageDataLoader::LoadAll("assets/data/stage_01.json", "assets/data/enemy_data.json");
    DebugRunPreset runPreset;
    runPreset.battle = setup;
    runPreset.battle.seed = UINT32_MAX;
    runPreset.run.completedAreas = 7;
    runPreset.run.destination = DebugRunDestination::Battle;
    runPreset.run.combatMode = RunCombatMode::PhaseTest;
    runPreset.run.stageId = "normal_002";
    runPreset.run.infiniteMoney = true;
    const auto presetJson = runPreset.ToJson();
    assert(DebugRunPreset::FromJson(presetJson, catalog, enemies, stages).ToJson() == presetJson);
    // Comparison is read-only, ignores generated IDs, and retains deck order/counts.
    auto comparisonCopy = runPreset;
    auto differences = CompareDebugRunPresets(runPreset, comparisonCopy);
    auto changedRow = [&](const std::string& key) {
        for (const auto& row : differences) if (row.key == key) return row.changed;
        assert(false); return false;
    };
    assert(std::none_of(differences.begin(), differences.end(), [](const auto& row) { return row.changed; }));
    comparisonCopy.battle.deck[0].instanceId += 1000;
    differences = CompareDebugRunPresets(runPreset, comparisonCopy);
    assert(!changedRow("deck/0"));
    comparisonCopy.run.completedAreas = 8;
    comparisonCopy.run.stageId.clear();
    comparisonCopy.run.infiniteMoney = false;
    comparisonCopy.battle.deck[0].upgradeLevel = 2;
    comparisonCopy.battle.deck[0].status.mass += 1;
    comparisonCopy.battle.deck[0].upgradeTable[0].attack += 3;
    comparisonCopy.battle.deck.push_back(comparisonCopy.battle.deck[0]);
    comparisonCopy.battle.relics[0] = !runPreset.battle.relics[0];
    differences = CompareDebugRunPresets(runPreset, comparisonCopy);
    for (const auto* key : {"progress", "stage", "infinite_money", "deck_count", "deck/0",
        "deck/0/upgrade_level", "deck/0/status/mass", "deck/0/upgrade_table/0/attack", "relic/0"})
        assert(changedRow(key));
    assert(!changedRow("seed"));
    const auto forward = differences;
    differences = CompareDebugRunPresets(comparisonCopy, runPreset);
    assert(differences.size() == forward.size());
    for (size_t i = 0; i < differences.size(); ++i)
    {
        // Missing slots use a direction-specific added/removed explanation.
        if (forward[i].before.find("なし") != std::string::npos || forward[i].after.find("なし") != std::string::npos) continue;
        assert(differences[i].before == forward[i].after && differences[i].after == forward[i].before);
    }
    comparisonCopy = runPreset;
    comparisonCopy.battle.deck.insert(comparisonCopy.battle.deck.begin(), ball("player_anchor"));
    const auto ordered = comparisonCopy;
    std::swap(comparisonCopy.battle.deck.front(), comparisonCopy.battle.deck.back());
    differences = CompareDebugRunPresets(ordered, comparisonCopy);
    assert(changedRow("deck/0") && !changedRow("deck_count"));
    comparisonCopy.battle.deck.clear();
    differences = CompareDebugRunPresets(runPreset, comparisonCopy);
    assert(changedRow("deck_count") && changedRow("deck/0"));
    assert(runPreset.ToJson() == presetJson);
    // Bad run fields and unavailable stages must fail before caller-owned settings are replaced.
    auto rejectRun = [&](nlohmann::json bad)
    {
        bool rejected = false;
        try { DebugRunPreset::FromJson(bad, catalog, enemies, stages); } catch (...) { rejected = true; }
        assert(rejected && runPreset.ToJson() == presetJson);
    };
    for (const auto& entry : std::vector<std::pair<std::string, nlohmann::json>>{
        {"completed_areas", -1}, {"completed_areas", 15}, {"completed_areas", 16},
        {"completed_areas", 1.5}, {"completed_areas", 4294967296ull},
        {"destination", "invalid"}, {"combat_mode", "future"},
        {"infinite_money", 1}, {"stage_id", "deleted_stage"}})
    {
        auto bad = presetJson; bad["run"][entry.first] = entry.second; rejectRun(bad);
    }
    auto badRun = presetJson; badRun["version"] = 2; rejectRun(badRun);
    badRun = presetJson; badRun["version"] = 1.0; rejectRun(badRun);
    badRun = presetJson; badRun["run"]["destination"] = "midboss"; rejectRun(badRun);
    badRun = presetJson; badRun["run"].erase("infinite_money"); rejectRun(badRun);
    badRun = presetJson; badRun["battle_setup"]["deck"][0]["definition_id"] = "missing_ball";
    // Other malformed battle fields are covered below; use empty deck here for a guaranteed invalid start.
    badRun["battle_setup"]["deck"] = nlohmann::json::array(); rejectRun(badRun);
    for (int destination = 0; destination <= 6; ++destination)
    {
        auto automatic = presetJson;
        automatic["run"]["stage_id"] = "";
        const char* ids[] = {"route", "battle", "midboss", "shop", "rest", "boss_preparation", "final_boss"};
        automatic["run"]["destination"] = ids[destination];
        automatic["run"]["completed_areas"] = destination >= 5 ? 15 : 0;
        assert(DebugRunPreset::FromJson(automatic, catalog, enemies, stages).ToJson() == automatic);
    }
    const std::filesystem::path runPresetsPath = "tools/runtime_tests/debug_run_presets/presets.json";
    std::filesystem::create_directories(runPresetsPath.parent_path());
    { std::ofstream file(runPresetsPath); file << R"({"format":"dx22_debug_run_presets","version":1,"presets":{}})"; }
    assert(DebugRunPresetStore::List(runPresetsPath).empty());
    const std::string japaneseName = "\xE6\xA4\x9C\xE8\xA8\xBC";
    DebugRunPresetStore::Save(runPresetsPath, "first", presetJson);
    DebugRunPresetStore::Save(runPresetsPath, japaneseName, presetJson);
    auto changedPreset = presetJson; changedPreset["run"]["infinite_money"] = false;
    DebugRunPresetStore::Save(runPresetsPath, "first", changedPreset);
    assert(DebugRunPresetStore::List(runPresetsPath).size() == 2);
    assert(DebugRunPresetStore::Load(runPresetsPath, "first") == changedPreset);
    assert(DebugRunPresetStore::Load(runPresetsPath, japaneseName) == presetJson);
    auto presetBytes = [&]()
    {
        std::ifstream file(runPresetsPath, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    };
    DebugRunPresetStore::Duplicate(runPresetsPath, "first", "comparison");
    assert(DebugRunPresetStore::List(runPresetsPath).size() == 3);
    assert(DebugRunPresetStore::Load(runPresetsPath, "comparison") == changedPreset);
    assert(DebugRunPresetStore::Load(runPresetsPath, "first") == changedPreset);
    auto independentCopy = changedPreset;
    independentCopy["battle_setup"]["deck"][0]["upgrade_level"] = 2;
    independentCopy["battle_setup"]["relics"][0] = false;
    independentCopy["run"]["completed_areas"] = 8;
    DebugRunPresetStore::Save(runPresetsPath, "comparison", independentCopy);
    assert(DebugRunPresetStore::Load(runPresetsPath, "comparison") == independentCopy);
    assert(DebugRunPresetStore::Load(runPresetsPath, "first") == changedPreset);
    DebugRunPresetStore::Duplicate(runPresetsPath, "comparison", japaneseName + "_copy");
    assert(DebugRunPresetStore::Load(runPresetsPath, japaneseName + "_copy") == independentCopy);
    const auto copiedBytes = presetBytes();
    for (const auto& target : std::vector<std::string>{"first", japaneseName, "", "   ", "bad\nname", std::string(129, 'x')})
    {
        bool rejectedCopy = false;
        try { DebugRunPresetStore::Duplicate(runPresetsPath, "first", target); } catch (...) { rejectedCopy = true; }
        assert(rejectedCopy && presetBytes() == copiedBytes);
    }
    bool rejectedCopy = false;
    try { DebugRunPresetStore::Duplicate(runPresetsPath, "missing", "new_copy"); } catch (...) { rejectedCopy = true; }
    assert(rejectedCopy && presetBytes() == copiedBytes);
    DebugRunPresetStore::Delete(runPresetsPath, "comparison");
    DebugRunPresetStore::Delete(runPresetsPath, japaneseName + "_copy");
    // Limits must reject the copy without replacing the original file.
    const auto limitPath = runPresetsPath.parent_path() / "copy_limits.json";
    nlohmann::json limitStore = {{"format", "dx22_debug_run_presets"}, {"version", 1}, {"presets", nlohmann::json::object()}};
    for (int i = 0; i < 64; ++i) limitStore["presets"]["entry" + std::to_string(i)] = {{"fixture", i}};
    auto rejectLimitedCopy = [&]()
    {
        const auto bytes = limitStore.dump();
        { std::ofstream file(limitPath, std::ios::binary); file << bytes; }
        bool rejected = false;
        try { DebugRunPresetStore::Duplicate(limitPath, "entry0", "extra"); } catch (...) { rejected = true; }
        assert(rejected);
        std::ifstream file(limitPath, std::ios::binary);
        assert(std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()) == bytes);
    };
    rejectLimitedCopy();
    limitStore["presets"] = {{"entry0", {{"large_fixture", std::string(2 * 1024 * 1024 + 100, 'x')}}}};
    rejectLimitedCopy();
    DebugRunPresetStore::Rename(runPresetsPath, "first", "renamed");
    assert(DebugRunPresetStore::List(runPresetsPath).size() == 2);
    assert(DebugRunPresetStore::Load(runPresetsPath, "renamed") == changedPreset);
    assert(DebugRunPresetStore::Load(runPresetsPath, japaneseName) == presetJson);
    const auto renamedBytes = presetBytes();
    DebugRunPresetStore::Rename(runPresetsPath, "renamed", "renamed");
    assert(presetBytes() == renamedBytes);
    for (const auto& target : std::vector<std::string>{japaneseName, "", "   ", "bad\nname", std::string(129, 'x')})
    {
        bool rejectedRename = false;
        try { DebugRunPresetStore::Rename(runPresetsPath, "renamed", target); } catch (...) { rejectedRename = true; }
        assert(rejectedRename && presetBytes() == renamedBytes);
    }
    bool missingRejected = false;
    try { DebugRunPresetStore::Rename(runPresetsPath, "missing", "new"); } catch (...) { missingRejected = true; }
    assert(missingRejected && presetBytes() == renamedBytes);
    DebugRunPresetStore::Delete(runPresetsPath, "renamed");
    assert(DebugRunPresetStore::List(runPresetsPath) == std::vector<std::string>{japaneseName});
    assert(DebugRunPresetStore::Load(runPresetsPath, japaneseName) == presetJson);
    const auto remainingBytes = presetBytes();
    missingRejected = false;
    try { DebugRunPresetStore::Delete(runPresetsPath, "renamed"); } catch (...) { missingRejected = true; }
    assert(missingRejected && presetBytes() == remainingBytes);
    DebugRunPresetStore::Rename(runPresetsPath, japaneseName, "last");
    assert(DebugRunPresetStore::Load(runPresetsPath, "last") == presetJson);
    DebugRunPresetStore::Delete(runPresetsPath, "last");
    assert(DebugRunPresetStore::List(runPresetsPath).empty());
    assert(std::filesystem::exists(runPresetsPath));
    // Restore the two fixtures for the malformed-file/invalid-name tests below.
    DebugRunPresetStore::Save(runPresetsPath, "first", changedPreset);
    DebugRunPresetStore::Save(runPresetsPath, japaneseName, presetJson);
    bool fileFailure = false;
    try { DebugRunPresetStore::Save(runPresetsPath, "   ", presetJson); } catch (...) { fileFailure = true; }
    assert(fileFailure && DebugRunPresetStore::Load(runPresetsPath, "first") == changedPreset);
    { std::ofstream file(runPresetsPath); file << "{broken"; }
    fileFailure = false;
    try { DebugRunPresetStore::Save(runPresetsPath, "new", presetJson); } catch (...) { fileFailure = true; }
    assert(fileFailure);
    { std::ifstream file(runPresetsPath); std::string bytes; std::getline(file, bytes); assert(bytes == "{broken"); }
    for (int operation : {0, 1, 2})
    {
        fileFailure = false;
        try
        {
            if (operation == 2) DebugRunPresetStore::Duplicate(runPresetsPath, "first", "new");
            else if (operation == 1) DebugRunPresetStore::Rename(runPresetsPath, "first", "new");
            else DebugRunPresetStore::Delete(runPresetsPath, "first");
        }
        catch (...) { fileFailure = true; }
        assert(fileFailure && presetBytes() == "{broken");
    }
    { std::ofstream file(runPresetsPath); file << std::string(4 * 1024 * 1024 + 1, ' '); }
    fileFailure = false;
    try { DebugRunPresetStore::Load(runPresetsPath, "first"); } catch (...) { fileFailure = true; }
    assert(fileFailure);
    ProgressionProfile debugProfile;
    roundtrip.ApplyProgressionTo(debugProfile);
    assert(debugProfile.highestUnlockedAscension == 7 &&
        debugProfile.selectedAscension == 5 &&
        debugProfile.IsAchievementUnlocked(AchievementId::FirstVictory));
    assert(roundtrip.DefaultStartingHp() == setup.maxHp - 5);
    assert(roundtrip.breakBallPositions.size() == 3 &&
        roundtrip.breakBallPositions[1].x == 11 &&
        roundtrip.breakBallPositions[1].z == -7);
    auto legacyJson = setup.ToJson();
    legacyJson["version"] = 1;
    legacyJson.erase("enemy_validation");
    legacyJson.erase("highest_unlocked_ascension");
    legacyJson.erase("selected_ascension");
    legacyJson.erase("achievements");
    legacyJson.erase("break_balls");
    const auto legacy = DebugBattleSetup::FromJson(legacyJson, catalog, enemies);
    assert(legacy.highestUnlockedAscension == 0 && legacy.selectedAscension == 0);
    assert(std::none_of(legacy.achievements.begin(), legacy.achievements.end(), [](bool value) { return value; }));
    assert(legacy.breakBallPositions.size() == 2);
    assert(!legacy.enemyValidation.enabled &&
        legacy.enemyValidation.hpMultiplier == 1.0f &&
        legacy.enemyValidation.enemyCount == 8 &&
        legacy.enemyValidation.layoutPattern ==
            EnemyValidationLayoutPattern::Random);
    PlayerDeck deck;
    deck.SetDefaultDeck(roundtrip.deck); deck.Seed(123); deck.ResetToDefault();
    assert(deck.GetRewardTargetCount() == 3);
    assert(deck.GetRewardTarget(0)->instanceId != deck.GetRewardTarget(1)->instanceId);
    setup.deck.resize(1);
    deck.SetDefaultDeck(setup.deck); deck.ResetToDefault();
    assert(deck.PrepareOffer() && deck.SelectOffer(0, -1));
    deck.MarkCurrentUsed(); assert(deck.DiscardCurrentIfUsed());
    assert(deck.PrepareOffer()); // 1個デッキが次ターンにも使える。

    int rejected = 0;
    auto reject = [&](nlohmann::json value) {
        bool failed = false;
        try { DebugBattleSetup::FromJson(value, catalog, enemies); }
        catch (const std::exception&) { failed = true; }
        assert(failed); ++rejected;
    };
    auto j = setup.ToJson(); j["hp"] = 0; reject(j);
    j = setup.ToJson(); j["hp"] = 51; reject(j);
    j = setup.ToJson(); j["deck"] = nlohmann::json::array(); reject(j);
    j = setup.ToJson(); j["enemy_validation"]["enabled"] = false;
    j["enemies"] = nlohmann::json::array(); reject(j);
    j = setup.ToJson(); j["enemies"][0]["id"] = "missing"; reject(j);
    j = setup.ToJson(); j["deck"][0]["definition_id"] = "missing"; reject(j);
    j = setup.ToJson(); j["seed"] = -1; reject(j);
    j = setup.ToJson(); j["player_x"] = 500; reject(j);
    j = setup.ToJson(); j["armor"] = 0; reject(j);
    j = setup.ToJson(); j["relics"] = nlohmann::json::array(); reject(j);
    j = setup.ToJson(); j["selected_ascension"] = 8; reject(j);
    j = setup.ToJson(); j["highest_unlocked_ascension"] = 11; reject(j);
    j = setup.ToJson(); j["achievements"] = nlohmann::json::array(); reject(j);
    j = setup.ToJson(); j["break_balls"] = nlohmann::json::array();
    for (int i = 0; i < 17; ++i) j["break_balls"].push_back({{"x", 0}, {"z", 0}});
    reject(j);
    j = setup.ToJson(); j["break_balls"][0]["x"] = 500; reject(j);
    j = setup.ToJson(); j["enemy_validation"]["hp_multiplier"] = 1.25; reject(j);
    j = setup.ToJson(); j["enemy_validation"]["enemy_count"] = 7; reject(j);
    j = setup.ToJson(); j["enemy_validation"]["layout_pattern"] = "grid"; reject(j);
    setup.enemyValidation.enabled = false;
    j = setup.ToJson(); j["enemies"][0]["hp"] = 99999; reject(j);
    auto invalid = setup; invalid.playerPosition.x = std::numeric_limits<float>::quiet_NaN(); assert(!invalid.Validate().empty());
    invalid = setup; invalid.deck[0].status.mass = std::numeric_limits<float>::infinity(); assert(!invalid.Validate().empty());
    invalid = setup; invalid.enemies = {foe("enemy_boss_core"), foe("enemy_boss_core")}; assert(!invalid.Validate().empty());
    // 接触・重なり不具合を再現する配置は許可する。
    invalid = setup; invalid.enemies[0].spawn.position = invalid.playerPosition; assert(invalid.Validate().empty());

    auto save = [&](const char* name) {
        assert(setup.Validate().empty());
        std::ofstream file(std::string("tools/runtime_tests/debug_mode/") + name);
        file << setup.ToJson().dump(2); assert(file.good());
    };
    setup.deck[0].status.attack = 999;
    setup.enemies[0].hp = 1;
    save("setup_win.json");
    setup.hp = 1; setup.relics.fill(false); setup.deck[0].status.attack = 0;
    setup.enemies[0].hp = setup.enemies[0].spawn.enemyData.maxHp = 999;
    setup.enemies[0].spawn.enemyData.status.attack = 999;
    save("setup_loss.json");
    setup.hp = 13; setup.deck = {ball("player_heavy")};
    setup.deck[0].upgradeLevel = 2; setup.deck[0].status = setup.deck[0].upgradeTable[1];
    setup.enemies = {foe("enemy_boss_core")}; setup.enemies[0].hp = 30;
    setup.enemies[0].spawn.position = {0, 1, 20}; setup.playerPosition = {0, 1, -10};
    setup.armor = 0; setup.breakShots = 1;
    save("setup_boss.json");
    assert(DebugBattleSetup::FromJson(setup.ToJson(), catalog, enemies).ToJson() == setup.ToJson());
    setup.deck = {ball("player_chain_impact")};
    setup.enemies[0].spawn.position = {10, 1, 0};
    setup.playerPosition = {0, 1, -10};
    setup.armor = 2; setup.breakShots = 0;
    setup.breakBallPositions = {{0, TableConfig::FIELD_HEIGHT, 0}};
    save("setup_break_chain.json");
    std::cout << "PASS: independent preset copies, collision/limit rejection, 105 debug run destinations, final boss transitions, infinite money purchases/reset, roundtrip, duplicate identities, 1-ball redraw, " << rejected << " malformed presets, nonfinite values, boss limit, overlap and fixtures\n";
}
