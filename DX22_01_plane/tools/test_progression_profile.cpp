#include "ProgressionProfile.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

int main()
{
    const auto path = std::filesystem::path("tools/runtime_tests/progression/profile.json");
    std::filesystem::create_directories(path.parent_path());
    std::filesystem::remove(path);
    ProgressionProfile p;
    assert(p.highestUnlockedAscension == 0 && p.IsBallUnlocked("player_standard"));
    assert(!p.IsBallUnlocked("player_pierce") && !p.IsBallUnlocked("player_bounce") && !p.IsBallUnlocked("player_anchor"));
    assert(!p.IsRelicUnlocked(RelicType::PierceBallCharger));
    ProgressionProfile nonBattleProfile;
    RunResultSnapshot nonBattleArea; nonBattleArea.areaProgress = 1;
    nonBattleProfile.RecordRun(nonBattleArea, 0);
    assert(!nonBattleProfile.IsAchievementUnlocked(AchievementId::FirstVictory));
    RunResultSnapshot run; run.areaProgress = 5; run.clearedBattles = 1; run.maximumTurnDamage = 15; run.midBossDefeats = 1;
    run.acquiredBallIds = {"a", "b", "c"};
    auto first = p.RecordRun(run, 0);
    assert(first.size() == 6 && p.totalRuns == 1 && p.highestArea == 5);
    assert(p.IsBallUnlocked("player_pierce") && p.IsBallUnlocked("player_bounce") && p.IsBallUnlocked("player_anchor"));
    assert(p.IsRelicUnlocked(RelicType::PierceBallCharger) && p.IsRelicUnlocked(RelicType::ExpandedBallOffer));
    assert(!p.IsRelicUnlocked(RelicType::BounceBallSpring));
    run.completed = run.finalBossDefeated = true;
    auto clear = p.RecordRun(run, 0);
    assert(p.totalRuns == 2 && p.totalClears == 1 && p.highestUnlockedAscension == 1 && p.selectedAscension == 1);
    assert(p.IsRelicUnlocked(RelicType::BounceBallSpring) && p.IsRelicUnlocked(RelicType::AnchorBallChain));
    assert(clear.size() == 2); // final-boss achievement plus A1.
    run.maximumTurnDamage = 0; run.acquiredBallIds.clear();
    p.RecordRun(run, 0); assert(p.highestUnlockedAscension == 1); // Lower difficulty cannot unlock A2.
    p.RecordRun(run, 1); assert(p.highestUnlockedAscension == 2 && p.selectedAscension == 2);
    p.Save(path);
    ProgressionProfile loaded; loaded.Load(path);
    assert(loaded.totalRuns == 4 && loaded.totalClears == 3 && loaded.highestUnlockedAscension == 2);
    assert(loaded.achievements == p.achievements);
    assert(ProgressionProfile::EnemyHpMultiplier(10) == 1.35f);
    assert(ProgressionProfile::EnemyAttackBonus(10) == 3);
    assert(ProgressionProfile::StartingHpPenalty(7) == 10);
    assert(ProgressionProfile::RestHealPenalty(8) == .10f);
    { std::ofstream bad(path); bad << "{broken"; }
    loaded.Load(path); assert(loaded.totalRuns == 0 && loaded.highestUnlockedAscension == 0);
    std::cout << "PASS progression achievements, unlock gates, ascension sequence/effects, persistence and corrupt fallback\n";
}
