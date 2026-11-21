#pragma once
#include "DebugBattleSetup.h"
#include "DebugRunSetup.h"
#include <filesystem>

// A reproducible run entry configuration, rather than a mid-shot checkpoint.
struct DebugRunPreset
{
    DebugRunSetup run;
    DebugBattleSetup battle;

    nlohmann::json ToJson() const;
    static DebugRunPreset FromJson(const nlohmann::json& value,
        const std::vector<PlayerBallData>& balls, const std::vector<EnemyData>& enemies,
        const std::vector<StageData>& stages);
};

struct DebugRunPresetComparisonRow
{
    std::string key;
    std::string label;
    std::string before;
    std::string after;
    bool changed = false;
};

// Read-only comparison of validated records. Deck slots retain their saved order;
// generated instance IDs do not describe a gameplay difference.
std::vector<DebugRunPresetComparisonRow> CompareDebugRunPresets(
    const DebugRunPreset& before, const DebugRunPreset& after);

class DebugRunPresetStore
{
public:
    static std::vector<std::string> List(const std::filesystem::path& path);
    static nlohmann::json Load(const std::filesystem::path& path, const std::string& name);
    static void Save(const std::filesystem::path& path, const std::string& name,
        const nlohmann::json& preset);
    static void Rename(const std::filesystem::path& path, const std::string& oldName,
        const std::string& newName);
    static void Duplicate(const std::filesystem::path& path, const std::string& sourceName,
        const std::string& newName);
    static void Delete(const std::filesystem::path& path, const std::string& name);
};
