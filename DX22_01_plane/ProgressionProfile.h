#pragma once
#include "GameTypes.h"
#include "RunResultSnapshot.h"
#include <array>
#include <filesystem>
#include <string>
#include <vector>

enum class AchievementId
{
    FirstVictory, DamageEight, AreaFive, MidBoss, Collector,
    DamageFifteen, FinalBoss, Count
};

struct AchievementDefinition
{
    AchievementId id;
    const char* key;
    const char* name;
    const char* condition;
    const char* reward;
};

inline const char* ProgressionUtf8(const char8_t* text) noexcept
{
    return reinterpret_cast<const char*>(text);
}

inline const std::array<AchievementDefinition, static_cast<size_t>(AchievementId::Count)> AchievementCatalog{{
    {AchievementId::FirstVictory, "first_victory", ProgressionUtf8(u8"最初の勝利"), ProgressionUtf8(u8"通常戦を1回クリアする"), ProgressionUtf8(u8"貫通分類のボール")},
    {AchievementId::DamageEight, "damage_eight", ProgressionUtf8(u8"連鎖の一打"), ProgressionUtf8(u8"1ショットで8ダメージ以上与える"), ProgressionUtf8(u8"衝撃加速装置")},
    {AchievementId::AreaFive, "area_five", ProgressionUtf8(u8"道半ば"), ProgressionUtf8(u8"エリア5へ到達する"), ProgressionUtf8(u8"反発分類のボール")},
    {AchievementId::MidBoss, "midboss", ProgressionUtf8(u8"中ボス撃破"), ProgressionUtf8(u8"中ボスを1体撃破する"), ProgressionUtf8(u8"アンカー分類のボール")},
    {AchievementId::Collector, "collector", ProgressionUtf8(u8"ボール収集家"), ProgressionUtf8(u8"1ランで新しいボールを3個獲得する"), ProgressionUtf8(u8"拡張ボールラック")},
    {AchievementId::DamageFifteen, "damage_fifteen", ProgressionUtf8(u8"決定打"), ProgressionUtf8(u8"1ショットで15ダメージ以上与える"), ProgressionUtf8(u8"貫通過給機")},
    {AchievementId::FinalBoss, "final_boss", ProgressionUtf8(u8"Armor突破"), ProgressionUtf8(u8"最終ボスを撃破する"), ProgressionUtf8(u8"高張力スプリング・アンカーチェーン・アセンション1")},
}};

class ProgressionProfile
{
public:
    static constexpr int MaximumAscension = 10;
    int totalRuns = 0;
    int totalClears = 0;
    int highestArea = 0;
    int highestUnlockedAscension = 0;
    int selectedAscension = 0;
    std::array<bool, static_cast<size_t>(AchievementId::Count)> achievements{};

    void Load(const std::filesystem::path& path = "saves/profile_progress.json");
    void Save(const std::filesystem::path& path = "saves/profile_progress.json") const;
    bool IsBallUnlocked(const std::string& id) const;
    bool IsRelicUnlocked(RelicType type) const;
    bool IsAchievementUnlocked(AchievementId id) const;
    std::vector<std::string> RecordRun(const RunResultSnapshot& result, int clearedAscension);
    static const char* AscensionRule(int level);
    static float EnemyHpMultiplier(int level);
    static int EnemyAttackBonus(int level);
    static int StartingHpPenalty(int level);
    static float RestHealPenalty(int level);
};
