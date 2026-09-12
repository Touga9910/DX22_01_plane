#include "DebugBattleSetup.h"
#include "PlayerDeck.h"
#include <cassert>
#include <fstream>
#include <limits>
#include <iostream>

int main()
{
    const auto catalog = PlayerBallDataLoader::Load("assets/data/player_status.json", {}, {}).ballDefinitions;
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
    assert(setup.Validate().empty());
    const auto roundtrip = DebugBattleSetup::FromJson(setup.ToJson(), catalog, enemies);
    assert(roundtrip.ToJson() == setup.ToJson());
    ProgressionProfile debugProfile;
    roundtrip.ApplyProgressionTo(debugProfile);
    assert(debugProfile.highestUnlockedAscension == 7 &&
        debugProfile.selectedAscension == 5 &&
        debugProfile.IsAchievementUnlocked(AchievementId::FirstVictory));
    assert(roundtrip.EffectiveMaxHp() == setup.maxHp - 5);
    assert(roundtrip.breakBallPositions.size() == 3 &&
        roundtrip.breakBallPositions[1].x == 11 &&
        roundtrip.breakBallPositions[1].z == -7);
    auto legacyJson = setup.ToJson();
    legacyJson.erase("highest_unlocked_ascension");
    legacyJson.erase("selected_ascension");
    legacyJson.erase("achievements");
    legacyJson.erase("break_balls");
    const auto legacy = DebugBattleSetup::FromJson(legacyJson, catalog, enemies);
    assert(legacy.highestUnlockedAscension == 0 && legacy.selectedAscension == 0);
    assert(std::none_of(legacy.achievements.begin(), legacy.achievements.end(), [](bool value) { return value; }));
    assert(legacy.breakBallPositions.size() == 2);
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
    j = setup.ToJson(); j["enemies"] = nlohmann::json::array(); reject(j);
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
    std::cout << "PASS: roundtrip, duplicate identities, 1-ball redraw, " << rejected << " malformed presets, nonfinite values, boss limit, overlap and fixtures\n";
}
