#include "StageLayoutEditor.h"
#include <cassert>
#include <fstream>
#include <iostream>

int main()
{
    using Json = nlohmann::json;
    auto catalog = StageDataLoader::LoadEnemyDefinitions("assets/data/enemy_data.json");
    assert(!catalog.empty());
    Json layout = {{"id", "editor_test"}, {"stage_type", "normal"}, {"difficulty", 3}, {"par", 4}, {"enemies", {
        {{"enemy_id", "enemy_normal"}, {"x", -30}, {"z", 12}},
        {{"enemy_id", "enemy_normal"}, {"x", -10}, {"z", 20}},
        {{"enemy_id", "enemy_normal"}, {"x", 15}, {"z", -18}},
        {{"enemy_id", "enemy_normal"}, {"x", 42}, {"z", 8}}}}};
    assert(StageLayoutEditor::Inspect(layout, catalog, 3)["valid"] == true);
    auto invalid = [&](Json value) { assert(StageLayoutEditor::Inspect(value, catalog, 3)["valid"] == false); };
    auto bad = layout; bad["enemies"][0]["x"] = 70; invalid(bad);
    bad = layout; bad["enemies"][0]["x"] = 0; bad["enemies"][0]["z"] = 0; invalid(bad);
    bad = layout; bad["enemies"][1] = bad["enemies"][0]; invalid(bad);
    bad = layout; bad["enemies"][0]["enemy_id"] = "unknown"; invalid(bad);
    bad = layout; bad["enemies"][0]["x"] = nullptr; invalid(bad);
    bad = layout; bad["enemies"] = Json::array(); invalid(bad);
    bad = layout; bad["difficulty"] = 1.5; invalid(bad);
    bad = layout; bad["par"] = 100; invalid(bad);
    bad = layout; bad["id"] = "../escape"; invalid(bad);
    bad = layout; bad["stage_type"] = "boss"; invalid(bad);
    bad = layout; bad["enemies"][0]["enemy_id"] = "enemy_boss_core"; invalid(bad);
    bad["stage_type"] = "boss"; assert(StageLayoutEditor::Inspect(bad, catalog, 3)["valid"] == true);
    bad["enemies"][1]["enemy_id"] = "enemy_boss_core"; invalid(bad);
    bad = layout; bad["enemies"] = Json::array();
    for (int i = 0; i < 33; ++i) bad["enemies"].push_back(layout["enemies"][0]); invalid(bad);

    const std::filesystem::path path = "tools/runtime_tests/stage_editor/test_stages.json";
    std::filesystem::create_directories(path.parent_path());
    Json untouched = {{"id", "untouched"}, {"stageType", "normal"}, {"difficulty", 1}, {"enemies", Json::array()}, {"future_field", 42}};
    Json root = {{"future_root", "keep"}, {"stages", {untouched}}};
    { std::ofstream file(path); file << root.dump(2); }
    StageLayoutEditor editor;
    editor.Load(path, StageLayoutEditor::Decode(layout, catalog));
    auto changed = layout; changed["enemies"][0]["x"] = -34;
    const auto firstRevision = editor.revision;
    editor.Propose(changed, firstRevision, catalog, 3);
    assert(editor.draft == layout); // AI never changes the draft before acceptance.
    assert(editor.Accept()); assert(editor.draft == StageLayoutEditor::Encode(StageLayoutEditor::Decode(changed, catalog)));
    editor.Undo(false); assert(editor.draft == layout);
    editor.Undo(true); assert(editor.draft["enemies"][0]["x"] == -34);
    bool stale = false;
    try { editor.Propose(layout, firstRevision, catalog, 3); } catch (...) { stale = true; }
    assert(stale);
    editor.Propose(layout, editor.revision, catalog, 3); editor.Replace(changed); // no-op retains revision
    editor.Undo(false); assert(!editor.Accept());
    editor.Replace(changed);
    editor.Save(path, catalog, 3);
    Json saved; { std::ifstream file(path); file >> saved; }
    assert(saved["future_root"] == "keep" && saved["stages"][0] == untouched);
    assert(saved["stages"][1]["preserveLayout"] == true);
    const auto loaded = StageDataLoader::LoadAll(path.string(), "assets/data/enemy_data.json");
    assert(loaded.back().preserveLayout && loaded.back().enemies.size() == 4);
    assert(StageLayoutEditor::Encode(loaded.back()) == editor.draft);
    assert(std::filesystem::exists(path.string() + ".editor.bak"));
    editor.Save(path, catalog, 3); // repeated replacement with an existing backup
    { std::ofstream file(path, std::ios::app); file << " "; }
    bool conflict = false;
    try { editor.Save(path, catalog, 3); } catch (...) { conflict = true; }
    assert(conflict);
    std::cout << "PASS: geometry, IDs, boss rules, AI revision/acceptance, undo, atomic save, preservation, reload, conflict guard\n";
}
