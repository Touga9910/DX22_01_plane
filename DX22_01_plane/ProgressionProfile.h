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

inline constexpr std::array<AchievementDefinition, static_cast<size_t>(AchievementId::Count)> AchievementCatalog{{
    {AchievementId::FirstVictory, "first_victory", "最初の勝利", "通常戦を1回クリアする", "貫通ボール"},
    {AchievementId::DamageEight, "damage_eight", "連鎖の一打", "1ショットで8ダメージ以上与える", "衝撃加速装置"},
    {AchievementId::AreaFive, "area_five", "道半ば", "エリア5へ到達する", "バウンドボール"},
    {AchievementId::MidBoss, "midboss", "中ボス撃破", "中ボスを1体撃破する", "アンカーボール"},
    {AchievementId::Collector, "collector", "ボール収集家", "1ランで新しいボールを3個獲得する", "拡張ボールラック"},
    {AchievementId::DamageFifteen, "damage_fifteen", "決定打", "1ショットで15ダメージ以上与える", "貫通過給機"},
    {AchievementId::FinalBoss, "final_boss", "Armor突破", "最終ボスを撃破する", "高張力スプリング・アンカーチェーン・アセンション1"},
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
