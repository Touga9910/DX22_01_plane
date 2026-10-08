#pragma once

#include "GameTypes.h"

#include <array>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

class PlayerDeck;
struct PlayerRunStatus;
class ProgressionProfile;
class RunProgressController;
class StageSelector;

// 1回のランを通して保持される状態の所有者。
// Gameは進行の指示だけを行い、ラン状態へはこの境界を通してアクセスする。
class RunController final
{
public:
    RunController();
    ~RunController();

    RunController(const RunController&) = delete;
    RunController& operator=(const RunController&) = delete;

    void ResetRuntimeState(const PlayerRunStatus& defaultStatus);

    PlayerRunStatus& Status();
    const PlayerRunStatus& Status() const;

    PlayerDeck& Deck();
    const PlayerDeck& Deck() const;

    std::array<bool, static_cast<std::size_t>(RelicType::Count)>& Relics();
    const std::array<bool, static_cast<std::size_t>(RelicType::Count)>& Relics() const;

    RunProgressController& Progress();
    const RunProgressController& Progress() const;

    StageSelector& StageSelection();
    const StageSelector& StageSelection() const;

    float& RestHealRatio();
    float RestHealRatio() const;

    bool CanRestHeal() const;
    int GetRestHealAmount() const;
    int GetRestHealPercent() const;
    bool RestHeal();
    void AddMoney(int amount);
    bool SpendMoney(int amount);
    bool UpgradeBall(int ballIndex);
    bool BuyShopBall(int catalogIndex, int cost);
    bool RemoveShopBall(int ballIndex, int cost);

    void BeginStageReward();
    bool CollectStageReward(int amount);
    int GetCurrentStageRewardMoney() const;
    bool IsStageRewardCollected() const;

    bool HasRelic(RelicType type) const;
    int GetOwnedRelicCount() const;
    bool GrantRelic(int relicIndex);
    bool BuyRelic(int relicIndex);
    std::vector<int> RollRelicOffers(
        int count,
        bool midBoss,
        const ProgressionProfile& progressionProfile);

    void SeedRelics(std::uint32_t seed);
    std::mt19937& RelicRandomEngine();
    const std::mt19937& RelicRandomEngine() const;

    std::vector<int>& ShopRelicOffers();
    const std::vector<int>& ShopRelicOffers() const;

    std::vector<int>& ShopBallOffers();
    const std::vector<int>& ShopBallOffers() const;

    std::vector<int>& MidBossRelicOffers();
    const std::vector<int>& MidBossRelicOffers() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
