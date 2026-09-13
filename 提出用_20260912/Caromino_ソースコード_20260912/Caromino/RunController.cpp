#include "RunController.h"

#include "ProgressionProfile.h"

#include <algorithm>
#include <cmath>

void RunController::ResetRuntimeState(
    const PlayerRunStatus& defaultStatus)
{
    m_Status = NormalizePlayerRunStatus(defaultStatus);
    m_Status.progress = 1;
    m_Status.SetSelectedStageId("");
    m_Status.SetLastStageId("");
    m_Deck.ResetToDefault();
    m_OwnedRelics.fill(false);
    m_ShopRelicOffers.clear();
    m_MidBossRelicOffers.clear();
}

bool RunController::CanRestHeal() const
{
    return m_Status.currentHp < m_Status.maxHp;
}

int RunController::GetRestHealAmount() const
{
    return (std::max)(
        1,
        static_cast<int>(std::ceil(
            static_cast<float>(m_Status.maxHp) *
            m_RestHealRatio)));
}

int RunController::GetRestHealPercent() const
{
    return static_cast<int>(
        std::lround(m_RestHealRatio * 100.0f));
}

bool RunController::RestHeal()
{
    if (!CanRestHeal())
    {
        return false;
    }

    m_Status.currentHp = (std::min)(
        m_Status.maxHp,
        m_Status.currentHp + GetRestHealAmount());
    return true;
}

void RunController::AddMoney(int amount)
{
    m_Status.money += (std::max)(0, amount);
    m_Status = NormalizePlayerRunStatus(m_Status);
}

bool RunController::SpendMoney(int amount)
{
    amount = (std::max)(0, amount);
    if (m_Status.money < amount)
    {
        return false;
    }

    m_Status.money -= amount;
    return true;
}

bool RunController::UpgradeBall(int ballIndex)
{
    PlayerBallData* ball = m_Deck.GetRewardTarget(ballIndex);
    if (ball == nullptr || !ball->CanUpgrade())
    {
        return false;
    }

    const BallUpgradeStep& upgrade =
        ball->upgradeTable[ball->upgradeLevel];
    ball->status = NormalizeBallStatus(upgrade);
    ++ball->upgradeLevel;
    return true;
}

bool RunController::BuyShopBall(int catalogIndex, int cost)
{
    cost = (std::max)(0, cost);
    if (m_Status.money < cost ||
        !m_Deck.AddCatalogBall(catalogIndex))
    {
        return false;
    }

    m_Status.money -= cost;
    return true;
}

bool RunController::RemoveShopBall(int ballIndex, int cost)
{
    cost = (std::max)(0, cost);
    if (m_Status.money < cost ||
        m_Deck.GetRewardTargetCount() <= PlayerDeck::MinimumDeckSize ||
        !m_Deck.RemoveRewardTarget(ballIndex))
    {
        return false;
    }

    m_Status.money -= cost;
    return true;
}

void RunController::BeginStageReward()
{
    m_CurrentStageRewardMoney = 0;
    m_IsStageRewardCollected = false;
}

bool RunController::CollectStageReward(int amount)
{
    if (m_IsStageRewardCollected)
    {
        return false;
    }

    m_CurrentStageRewardMoney = (std::max)(0, amount);
    AddMoney(m_CurrentStageRewardMoney);
    m_IsStageRewardCollected = true;
    return true;
}

bool RunController::HasRelic(RelicType type) const
{
    const std::size_t index = static_cast<std::size_t>(type);
    return index < m_OwnedRelics.size() && m_OwnedRelics[index];
}

int RunController::GetOwnedRelicCount() const
{
    return static_cast<int>(std::count(
        m_OwnedRelics.begin(),
        m_OwnedRelics.end(),
        true));
}

bool RunController::GrantRelic(int relicIndex)
{
    if (relicIndex < 0 ||
        relicIndex >= static_cast<int>(RelicCatalog.size()))
    {
        return false;
    }

    const RelicDefinition& relic =
        RelicCatalog[static_cast<std::size_t>(relicIndex)];
    if (HasRelic(relic.type))
    {
        return false;
    }

    m_OwnedRelics[static_cast<std::size_t>(relic.type)] = true;
    return true;
}

bool RunController::BuyRelic(int relicIndex)
{
    if (relicIndex < 0 ||
        relicIndex >= static_cast<int>(RelicCatalog.size()))
    {
        return false;
    }

    const RelicDefinition& relic =
        RelicCatalog[static_cast<std::size_t>(relicIndex)];
    const int cost = (std::max)(0, relic.price);
    if (HasRelic(relic.type) || m_Status.money < cost)
    {
        return false;
    }

    m_Status.money -= cost;
    m_OwnedRelics[static_cast<std::size_t>(relic.type)] = true;
    return true;
}

std::vector<int> RunController::RollRelicOffers(
    int count,
    bool midBoss,
    const ProgressionProfile& progressionProfile)
{
    std::vector<int> candidates;
    for (int index = 0;
        index < static_cast<int>(RelicCatalog.size());
        ++index)
    {
        const RelicDefinition& relic =
            RelicCatalog[static_cast<std::size_t>(index)];
        const int weight =
            midBoss ? relic.midBossWeight : relic.shopWeight;
        if (progressionProfile.IsRelicUnlocked(relic.type) &&
            !HasRelic(relic.type) &&
            weight > 0)
        {
            candidates.push_back(index);
        }
    }

    std::vector<int> offers;
    while (!candidates.empty() &&
        static_cast<int>(offers.size()) < count)
    {
        int totalWeight = 0;
        for (const int index : candidates)
        {
            const RelicDefinition& relic =
                RelicCatalog[static_cast<std::size_t>(index)];
            totalWeight +=
                midBoss ? relic.midBossWeight : relic.shopWeight;
        }

        std::uniform_int_distribution<int> distribution(1, totalWeight);
        int roll = distribution(m_RelicRandomEngine);
        std::size_t selected = 0;
        for (; selected < candidates.size(); ++selected)
        {
            const RelicDefinition& relic = RelicCatalog[
                static_cast<std::size_t>(candidates[selected])];
            roll -= midBoss ? relic.midBossWeight : relic.shopWeight;
            if (roll <= 0)
            {
                break;
            }
        }

        offers.push_back(candidates[selected]);
        candidates.erase(candidates.begin() + selected);
    }

    return offers;
}
