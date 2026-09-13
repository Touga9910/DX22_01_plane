#pragma once

#include "GameTypes.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "RunProgressController.h"
#include "StageSelector.h"

#include <array>
#include <cstdint>
#include <random>
#include <vector>

class ProgressionProfile;

// 1回のランを通して保持される状態の所有者。
// Gameは進行の指示だけを行い、ラン状態へはこの境界を通してアクセスする。
class RunController final
{
public:
    void ResetRuntimeState(const PlayerRunStatus& defaultStatus);

    PlayerRunStatus& Status() { return m_Status; }
    const PlayerRunStatus& Status() const { return m_Status; }

    PlayerDeck& Deck() { return m_Deck; }
    const PlayerDeck& Deck() const { return m_Deck; }

    std::array<bool, static_cast<std::size_t>(RelicType::Count)>& Relics()
    {
        return m_OwnedRelics;
    }

    const std::array<bool, static_cast<std::size_t>(RelicType::Count)>& Relics() const
    {
        return m_OwnedRelics;
    }

    RunProgressController& Progress() { return m_Progress; }
    const RunProgressController& Progress() const { return m_Progress; }

    StageSelector& StageSelection() { return m_StageSelector; }
    const StageSelector& StageSelection() const { return m_StageSelector; }

    float& RestHealRatio() { return m_RestHealRatio; }
    float RestHealRatio() const { return m_RestHealRatio; }

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
    int GetCurrentStageRewardMoney() const
    {
        return m_CurrentStageRewardMoney;
    }
    bool IsStageRewardCollected() const
    {
        return m_IsStageRewardCollected;
    }

    bool HasRelic(RelicType type) const;
    int GetOwnedRelicCount() const;
    bool GrantRelic(int relicIndex);
    bool BuyRelic(int relicIndex);
    std::vector<int> RollRelicOffers(
        int count,
        bool midBoss,
        const ProgressionProfile& progressionProfile);

    void SeedRelics(std::uint32_t seed) { m_RelicRandomEngine.seed(seed); }
    std::mt19937& RelicRandomEngine() { return m_RelicRandomEngine; }

    std::vector<int>& ShopRelicOffers() { return m_ShopRelicOffers; }
    const std::vector<int>& ShopRelicOffers() const { return m_ShopRelicOffers; }

    std::vector<int>& MidBossRelicOffers() { return m_MidBossRelicOffers; }
    const std::vector<int>& MidBossRelicOffers() const { return m_MidBossRelicOffers; }

private:
    PlayerRunStatus m_Status{};
    PlayerDeck m_Deck{};
    std::array<bool, static_cast<std::size_t>(RelicType::Count)> m_OwnedRelics{};
    RunProgressController m_Progress{};
    StageSelector m_StageSelector{};
    float m_RestHealRatio = 0.25f;
    std::vector<int> m_ShopRelicOffers{};
    std::vector<int> m_MidBossRelicOffers{};
    std::mt19937 m_RelicRandomEngine{ std::random_device{}() };
    int m_CurrentStageRewardMoney = 0;
    bool m_IsStageRewardCollected = false;
};
