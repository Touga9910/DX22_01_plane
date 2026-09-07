#include "ProgressionProfile.h"
#pragma execution_character_set("utf-8")
#include "json/json.hpp"
#include <Windows.h>
#include <algorithm>
#include <fstream>
#include <stdexcept>

using nlohmann::json;
namespace
{
    size_t Index(AchievementId id) { return static_cast<size_t>(id); }
    bool Satisfied(AchievementId id, const RunResultSnapshot& r)
    {
        switch (id)
        {
        case AchievementId::FirstVictory: return r.clearedBattles >= 1;
        case AchievementId::DamageEight: return r.maximumTurnDamage >= 8;
        case AchievementId::AreaFive: return r.areaProgress >= 5;
        case AchievementId::MidBoss: return r.midBossDefeats >= 1;
        case AchievementId::Collector: return r.acquiredBallIds.size() >= 3;
        case AchievementId::DamageFifteen: return r.maximumTurnDamage >= 15;
        case AchievementId::FinalBoss: return r.completed && r.finalBossDefeated;
        default: return false;
        }
    }
}

void ProgressionProfile::Load(const std::filesystem::path& path)
{
    *this = ProgressionProfile{};
    if (!std::filesystem::exists(path)) return;
    try
    {
        if (std::filesystem::file_size(path) > 1024 * 1024) throw std::runtime_error("profile too large");
        std::ifstream file(path); json root; file >> root;
        if (root.at("version").get<int>() != 1) throw std::runtime_error("unsupported profile version");
        totalRuns = std::clamp(root.value("total_runs", 0), 0, 1000000);
        totalClears = std::clamp(root.value("total_clears", 0), 0, totalRuns);
        highestArea = std::clamp(root.value("highest_area", 0), 0, 15);
        highestUnlockedAscension = std::clamp(root.value("highest_unlocked_ascension", 0), 0, MaximumAscension);
        selectedAscension = std::clamp(root.value("selected_ascension", 0), 0, highestUnlockedAscension);
        const auto values = root.value("achievements", json::array());
        if (!values.is_array()) throw std::runtime_error("achievements must be an array");
        for (const auto& value : values)
        {
            if (!value.is_string()) throw std::runtime_error("achievement key must be a string");
            const auto key = value.get<std::string>();
            for (const auto& definition : AchievementCatalog)
                if (key == definition.key) achievements[Index(definition.id)] = true;
        }
    }
    catch (...)
    {
        *this = ProgressionProfile{}; // A broken profile never affects a new run.
    }
}

void ProgressionProfile::Save(const std::filesystem::path& path) const
{
    std::filesystem::create_directories(path.parent_path());
    json unlocked = json::array();
    for (const auto& definition : AchievementCatalog)
        if (IsAchievementUnlocked(definition.id)) unlocked.push_back(definition.key);
    const json root = {{"version", 1}, {"total_runs", totalRuns}, {"total_clears", totalClears},
        {"highest_area", highestArea}, {"highest_unlocked_ascension", highestUnlockedAscension},
        {"selected_ascension", selectedAscension}, {"achievements", unlocked}};
    auto temporary = path; temporary += L".tmp";
    { std::ofstream file(temporary, std::ios::binary | std::ios::trunc); file << root.dump(2) << '\n'; file.close(); if (!file) throw std::runtime_error("profile write failed"); }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("profile replace failed");
}

bool ProgressionProfile::IsAchievementUnlocked(AchievementId id) const
{
    return id >= AchievementId::FirstVictory && id < AchievementId::Count && achievements[Index(id)];
}
bool ProgressionProfile::IsBallUnlocked(const std::string& id) const
{
    if (id == "player_pierce") return IsAchievementUnlocked(AchievementId::FirstVictory);
    if (id == "player_bounce") return IsAchievementUnlocked(AchievementId::AreaFive);
    if (id == "player_anchor") return IsAchievementUnlocked(AchievementId::MidBoss);
    return id == "player_standard" || id == "player_heavy";
}
bool ProgressionProfile::IsRelicUnlocked(RelicType type) const
{
    if (type == RelicType::CollisionAttackUp) return IsAchievementUnlocked(AchievementId::DamageEight);
    if (type == RelicType::PierceBallCharger) return IsAchievementUnlocked(AchievementId::DamageFifteen);
    if (type == RelicType::BounceBallSpring || type == RelicType::AnchorBallChain)
        return IsAchievementUnlocked(AchievementId::FinalBoss);
    if (type == RelicType::ExpandedBallOffer) return IsAchievementUnlocked(AchievementId::Collector);
    return true;
}

std::vector<std::string> ProgressionProfile::RecordRun(const RunResultSnapshot& result, int clearedAscension)
{
    ++totalRuns; highestArea = (std::max)(highestArea, result.areaProgress);
    std::vector<std::string> unlocked;
    for (const auto& definition : AchievementCatalog)
        if (!IsAchievementUnlocked(definition.id) && Satisfied(definition.id, result))
        {
            achievements[Index(definition.id)] = true;
            unlocked.push_back(std::string("実績「") + definition.name + "」達成：" + definition.reward + "を解放");
        }
    if (result.completed && result.finalBossDefeated)
    {
        ++totalClears;
        if (clearedAscension == highestUnlockedAscension && highestUnlockedAscension < MaximumAscension)
        {
            ++highestUnlockedAscension; selectedAscension = highestUnlockedAscension;
            unlocked.push_back("アセンション" + std::to_string(highestUnlockedAscension) + "を解放");
        }
    }
    return unlocked;
}

const char* ProgressionProfile::AscensionRule(int level)
{
    static const char* rules[] = {"通常ルール", "敵HP +10%", "開始・最大HP -5", "敵攻撃力 +1", "休憩回復 25%→20%",
        "敵HP +20%（合計）", "敵攻撃力 +2（合計）", "開始・最大HP -10（合計）", "休憩回復 15%", "敵HP +35%（合計）", "敵攻撃力 +3（合計）"};
    return rules[std::clamp(level, 0, MaximumAscension)];
}
float ProgressionProfile::EnemyHpMultiplier(int level) { return level >= 9 ? 1.35f : level >= 5 ? 1.20f : level >= 1 ? 1.10f : 1.0f; }
int ProgressionProfile::EnemyAttackBonus(int level) { return level >= 10 ? 3 : level >= 6 ? 2 : level >= 3 ? 1 : 0; }
int ProgressionProfile::StartingHpPenalty(int level) { return level >= 7 ? 10 : level >= 2 ? 5 : 0; }
float ProgressionProfile::RestHealPenalty(int level) { return level >= 8 ? .10f : level >= 4 ? .05f : 0.0f; }
