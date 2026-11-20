#include "../GameSaveManager.h"
#include "../GameRunSaveState.h"
#include "../json/json.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include "test_checks.h"

namespace fs = std::filesystem;
using nlohmann::json;
const fs::path path = "saves/run_save.json";

std::string Read()
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void Write(const std::string& bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << bytes;
    assert(file.good());
}

int main()
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
    assert(GameSaveManager::Remove());
    assert(!GameSaveManager::Inspect().exists);
    std::cout << "PASS: real save/load, deck IDs, map, RNG state, invalid/debug save rejection, atomic failure preservation, corrupt/truncated/unsupported saves, recovery\n";
}
