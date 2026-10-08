#include "RunController.h"

#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "ProgressionProfile.h"
#include "RunProgressController.h"
#include "StageSelector.h"

#include <algorithm>
#include <cmath>

struct RunController::Impl
{
    PlayerRunStatus status{};
    PlayerDeck deck{};
    std::array<bool, static_cast<std::size_t>(RelicType::Count)> ownedRelics{};
    RunProgressController progress{};
    StageSelector stageSelector{};
    float restHealRatio = 0.25f;
    std::vector<int> shopBallOffers{};
    std::vector<int> shopRelicOffers{};
    std::vector<int> midBossRelicOffers{};
    std::mt19937 relicRandomEngine{ std::random_device{}() };
    int currentStageRewardMoney = 0;
    bool isStageRewardCollected = false;
};

RunController::RunController()
    : m_Impl(std::make_unique<Impl>())
{
}

RunController::~RunController() = default;

PlayerRunStatus& RunController::Status()
{
    return m_Impl->status;
}

const PlayerRunStatus& RunController::Status() const
{
    return m_Impl->status;
}

PlayerDeck& RunController::Deck()
{
    return m_Impl->deck;
}

const PlayerDeck& RunController::Deck() const
{
    return m_Impl->deck;
}

std::array<bool, static_cast<std::size_t>(RelicType::Count)>&
RunController::Relics()
{
    return m_Impl->ownedRelics;
}

const std::array<bool, static_cast<std::size_t>(RelicType::Count)>&
RunController::Relics() const
{
    return m_Impl->ownedRelics;
}

RunProgressController& RunController::Progress()
{
    return m_Impl->progress;
}

const RunProgressController& RunController::Progress() const
{
    return m_Impl->progress;
}

StageSelector& RunController::StageSelection()
{
    return m_Impl->stageSelector;
}

const StageSelector& RunController::StageSelection() const
{
    return m_Impl->stageSelector;
}

float& RunController::RestHealRatio()
{
    return m_Impl->restHealRatio;
}

float RunController::RestHealRatio() const
{
    return m_Impl->restHealRatio;
}

void RunController::ResetRuntimeState(
    const PlayerRunStatus& defaultStatus)
{
    m_Impl->status = NormalizePlayerRunStatus(defaultStatus);
    m_Impl->status.progress = 1;
    m_Impl->status.SetSelectedStageId("");
    m_Impl->status.SetLastStageId("");
    m_Impl->deck.ResetToDefault();
    m_Impl->ownedRelics.fill(false);
    m_Impl->shopBallOffers.clear();
    m_Impl->shopRelicOffers.clear();
    m_Impl->midBossRelicOffers.clear();
}

bool RunController::CanRestHeal() const
{
    return m_Impl->status.currentHp < m_Impl->status.maxHp;
}

int RunController::GetRestHealAmount() const
{
    return (std::max)(
        1,
        static_cast<int>(std::ceil(
            static_cast<float>(m_Impl->status.maxHp) *
            m_Impl->restHealRatio)));
}

int RunController::GetRestHealPercent() const
{
    return static_cast<int>(
        std::lround(m_Impl->restHealRatio * 100.0f));
}

bool RunController::RestHeal()
{
    if (!CanRestHeal())
    {
        return false;
    }

    m_Impl->status.currentHp = (std::min)(
        m_Impl->status.maxHp,
        m_Impl->status.currentHp + GetRestHealAmount());
    return true;
}

void RunController::AddMoney(int amount)
{
    m_Impl->status.money += (std::max)(0, amount);
    m_Impl->status = NormalizePlayerRunStatus(m_Impl->status);
}

bool RunController::SpendMoney(int amount)
{
    amount = (std::max)(0, amount);
    if (m_Impl->status.money < amount)
    {
        return false;
    }

    m_Impl->status.money -= amount;
    return true;
}

bool RunController::UpgradeBall(int ballIndex)
{
    PlayerBallData* ball = m_Impl->deck.GetRewardTarget(ballIndex);
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
    if (m_Impl->status.money < cost ||
        !m_Impl->deck.AddCatalogBall(catalogIndex))
    {
        return false;
    }

    m_Impl->status.money -= cost;
    return true;
}

bool RunController::RemoveShopBall(int ballIndex, int cost)
{
    cost = (std::max)(0, cost);
    if (m_Impl->status.money < cost ||
        m_Impl->deck.GetRewardTargetCount() <= PlayerDeck::MinimumDeckSize ||
        !m_Impl->deck.RemoveRewardTarget(ballIndex))
    {
        return false;
    }

    m_Impl->status.money -= cost;
    return true;
}

void RunController::BeginStageReward()
{
    m_Impl->currentStageRewardMoney = 0;
    m_Impl->isStageRewardCollected = false;
}

bool RunController::CollectStageReward(int amount)
{
    if (m_Impl->isStageRewardCollected)
    {
        return false;
    }

    m_Impl->currentStageRewardMoney = (std::max)(0, amount);
    AddMoney(m_Impl->currentStageRewardMoney);
    m_Impl->isStageRewardCollected = true;
    return true;
}

int RunController::GetCurrentStageRewardMoney() const
{
    return m_Impl->currentStageRewardMoney;
}

bool RunController::IsStageRewardCollected() const
{
    return m_Impl->isStageRewardCollected;
}

bool RunController::HasRelic(RelicType type) const
{
    const std::size_t index = static_cast<std::size_t>(type);
    return index < m_Impl->ownedRelics.size() &&
        m_Impl->ownedRelics[index];
}

int RunController::GetOwnedRelicCount() const
{
    return static_cast<int>(std::count(
        m_Impl->ownedRelics.begin(),
        m_Impl->ownedRelics.end(),
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

    m_Impl->ownedRelics[static_cast<std::size_t>(relic.type)] = true;
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
    if (HasRelic(relic.type) || m_Impl->status.money < cost)
    {
        return false;
    }

    m_Impl->status.money -= cost;
    m_Impl->ownedRelics[static_cast<std::size_t>(relic.type)] = true;
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
        int roll = distribution(m_Impl->relicRandomEngine);
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

void RunController::SeedRelics(std::uint32_t seed)
{
    m_Impl->relicRandomEngine.seed(seed);
}

std::mt19937& RunController::RelicRandomEngine()
{
    return m_Impl->relicRandomEngine;
}

const std::mt19937& RunController::RelicRandomEngine() const
{
    return m_Impl->relicRandomEngine;
}

std::vector<int>& RunController::ShopRelicOffers()
{
    return m_Impl->shopRelicOffers;
}

const std::vector<int>& RunController::ShopRelicOffers() const
{
    return m_Impl->shopRelicOffers;
}

std::vector<int>& RunController::ShopBallOffers()
{
    return m_Impl->shopBallOffers;
}

const std::vector<int>& RunController::ShopBallOffers() const
{
    return m_Impl->shopBallOffers;
}

std::vector<int>& RunController::MidBossRelicOffers()
{
    return m_Impl->midBossRelicOffers;
}

const std::vector<int>& RunController::MidBossRelicOffers() const
{
    return m_Impl->midBossRelicOffers;
}
