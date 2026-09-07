#pragma once
#include "StageDataLoader.h"
#include "TableConfig.h"
#include "json/json.hpp"
#include <cstdint>
#include <filesystem>

// Shared authoring model: UI and MCP proposals use the same parser and checks.
class StageLayoutEditor
{
public:
    using Json = nlohmann::json;
    Json draft, proposal;
    std::uint64_t revision = 0;
    std::uint64_t proposalRevision = 0;
    std::vector<Json> undo, redo;
    std::string sourceBytes;
    std::string message;
    int selected = -1;
    int palette = 0;
    bool snap = true;
    bool placing = false;
    bool dragging = false;
    Json dragStart;
    char name[65] = {};

    static Json Encode(const StageData& stage);
    static StageData Decode(const Json& value, const std::vector<EnemyData>& catalog);
    static Json Inspect(const Json& value, const std::vector<EnemyData>& catalog, float playerRadius);
    void Replace(const Json& value);
    void Undo(bool forward);
    void Load(const std::filesystem::path& path, const StageData& stage);
    void Save(const std::filesystem::path& path, const std::vector<EnemyData>& catalog, float playerRadius);
    void Propose(const Json& value, std::uint64_t expected, const std::vector<EnemyData>& catalog, float playerRadius);
    bool Accept();
    Json Snapshot(const std::vector<EnemyData>& catalog, float playerRadius) const;
};
