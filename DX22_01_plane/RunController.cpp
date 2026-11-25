#include "RunController.h"

#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "ProgressionProfile.h"
#include "RunProgressController.h"
#include "StageSelector.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

struct RunController::Impl
{
    PlayerRunStatus status{};
    PlayerDeck deck{};
    std::array<bool, static_cast<std::size_t>(RelicType::Count)> ownedRelics{};
    RunProgressController progress{};
    StageSelector stageSelector{};
    float restHealRatio = 0.30f;
    std::vector<int> shopBallOffers{};
    std::vector<int> shopRelicOffers{};
    std::vector<int> midBossRelicOffers{};
    std::mt19937 relicRandomEngine{ std::random_device{}() };
    int currentStageRewardMoney = 0;
    bool isStageRewardCollected = false;
    bool debugInfiniteMoney = false;
	RunCombatMode combatMode = RunCombatMode::Legacy;
	PhaseRunProgressionMode phaseProgressionMode =
		PhaseRunProgressionMode::EncounterCount;
};

/// <summary>
/// ランの状態を保持する内部実装を初期化する。
/// </summary>
RunController::RunController()
    : m_Impl(std::make_unique<Impl>())
{
}

/// <summary>
/// ランの内部実装とそこで保持するリソースを破棄する。
/// </summary>
RunController::~RunController() = default;

/// <summary>
/// プレイヤーのラン状態への変更可能な参照を返す。
/// </summary>
PlayerRunStatus& RunController::Status()
{
    return m_Impl->status;
}

/// <summary>
/// プレイヤーのラン状態を読み取り専用の参照で返す。
/// </summary>
const PlayerRunStatus& RunController::Status() const
{
    return m_Impl->status;
}

/// <summary>
/// ランで使用中のデッキへの変更可能な参照を返す。
/// </summary>
PlayerDeck& RunController::Deck()
{
    return m_Impl->deck;
}

/// <summary>
/// ランで使用中のデッキを読み取り専用の参照で返す。
/// </summary>
const PlayerDeck& RunController::Deck() const
{
    return m_Impl->deck;
}

/// <summary>
/// 所持レリックのフラグ一覧への変更可能な参照を返す。
/// </summary>
std::array<bool, static_cast<std::size_t>(RelicType::Count)>&
RunController::Relics()
{
    return m_Impl->ownedRelics;
}

/// <summary>
/// 所持レリックのフラグ一覧を読み取り専用の参照で返す。
/// </summary>
const std::array<bool, static_cast<std::size_t>(RelicType::Count)>&
RunController::Relics() const
{
    return m_Impl->ownedRelics;
}

/// <summary>
/// ルートと戦闘の進行状態への変更可能な参照を返す。
/// </summary>
RunProgressController& RunController::Progress()
{
    return m_Impl->progress;
}

/// <summary>
/// ルートと戦闘の進行状態を読み取り専用の参照で返す。
/// </summary>
const RunProgressController& RunController::Progress() const
{
    return m_Impl->progress;
}

/// <summary>
/// ステージ選択を管理するオブジェクトへの変更可能な参照を返す。
/// </summary>
StageSelector& RunController::StageSelection()
{
    return m_Impl->stageSelector;
}

/// <summary>
/// ステージ選択を管理するオブジェクトを読み取り専用の参照で返す。
/// </summary>
const StageSelector& RunController::StageSelection() const
{
    return m_Impl->stageSelector;
}

/// <summary>
/// 休憩時のHP回復割合への変更可能な参照を返す。
/// </summary>
float& RunController::RestHealRatio()
{
    return m_Impl->restHealRatio;
}

/// <summary>
/// 休憩時に使うHP回復割合を返す。
/// </summary>
float RunController::RestHealRatio() const
{
    return m_Impl->restHealRatio;
}

/// <summary>
/// 既定のプレイヤー状態とデッキに戻し、所持レリック、候補一覧、戦闘モードを初期化する。
/// </summary>
void RunController::ResetRuntimeState(
    const PlayerRunStatus& defaultStatus)
{
    m_Impl->status = NormalizePlayerRunStatus(defaultStatus);
    m_Impl->debugInfiniteMoney = false;
    m_Impl->status.progress = 1;
    m_Impl->status.SetSelectedStageId("");
    m_Impl->status.SetLastStageId("");
    m_Impl->deck.ResetToDefault();
    m_Impl->ownedRelics.fill(false);
    m_Impl->shopBallOffers.clear();
    m_Impl->shopRelicOffers.clear();
    m_Impl->midBossRelicOffers.clear();
	m_Impl->combatMode = RunCombatMode::Legacy;
	m_Impl->phaseProgressionMode = PhaseRunProgressionMode::EncounterCount;
}

/// <summary>
/// ラン中に使う戦闘モードを設定する。
/// </summary>
void RunController::SetCombatMode(RunCombatMode mode)
{
	m_Impl->combatMode = mode;
}

/// <summary>
/// ラン中に使う現在の戦闘モードを返す。
/// </summary>
RunCombatMode RunController::GetCombatMode() const
{
	return m_Impl->combatMode;
}

/// <summary>
/// 戦闘フェーズの進行方式を設定する。
/// </summary>
void RunController::SetPhaseProgressionMode(PhaseRunProgressionMode mode)
{
	m_Impl->phaseProgressionMode = mode;
}

/// <summary>
/// 現在設定されている戦闘フェーズの進行方式を返す。
/// </summary>
PhaseRunProgressionMode RunController::GetPhaseProgressionMode() const
{
	return m_Impl->phaseProgressionMode;
}

/// <summary>
/// 現在HPが最大HP未満で、休憩による回復が可能かを返す。
/// </summary>
bool RunController::CanRestHeal() const
{
    return m_Impl->status.currentHp < m_Impl->status.maxHp;
}

/// <summary>
/// 最大HPと休憩時の回復割合から、HPの回復量を計算する。
/// </summary>
int RunController::GetRestHealAmount() const
{
    return (std::max)(
        1,
        static_cast<int>(std::ceil(
            static_cast<float>(m_Impl->status.maxHp) *
            m_Impl->restHealRatio)));
}

/// <summary>
/// 休憩時の回復割合を表示用の整数パーセントに変換する。
/// </summary>
int RunController::GetRestHealPercent() const
{
    return static_cast<int>(
        std::lround(m_Impl->restHealRatio * 100.0f));
}

/// <summary>
/// 休憩による回復を適用し、現在HPを最大HPまでの範囲で増やす。
/// </summary>
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

/// <summary>
/// 負の加算を除外して所持金を増やし、無限資金モードでは所持金を999999に設定する。
/// </summary>
void RunController::AddMoney(int amount)
{
    if (m_Impl->debugInfiniteMoney) { m_Impl->status.money = 999999; return; }
    const std::int64_t total = static_cast<std::int64_t>(m_Impl->status.money) +
        (std::max)(0, amount);
    m_Impl->status.money = static_cast<int>(std::clamp(
        total, std::int64_t{0}, std::int64_t{PlayerRunStatus::MaximumMoney}));
    m_Impl->status = NormalizePlayerRunStatus(m_Impl->status);
}

/// <summary>
/// 所持金で支出を賄えるかを調べ、可能なら支払う。
/// </summary>
bool RunController::SpendMoney(int amount)
{
    if (m_Impl->debugInfiniteMoney) return true;
    amount = (std::max)(0, amount);
    if (m_Impl->status.money < amount)
    {
        return false;
    }

    m_Impl->status.money -= amount;
    return true;
}

/// <summary>
/// デバッグ用の無限資金モードを設定し、有効化時に所持金を補充する。
/// </summary>
void RunController::SetDebugInfiniteMoney(bool enabled)
{
    m_Impl->debugInfiniteMoney = enabled;
    if (enabled) m_Impl->status.money = 999999;
}

/// <summary>
/// 指定された球を次の強化段階に進め、対応するステータスを適用する。
/// </summary>
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

/// <summary>
/// 所持金を確認してカタログの球をデッキに追加し、代金を支払う。
/// </summary>
bool RunController::BuyShopBall(int catalogIndex, int cost)
{
    cost = (std::max)(0, cost);
    if (m_Impl->status.money < cost ||
        !m_Impl->deck.AddCatalogBall(catalogIndex))
    {
        return false;
    }

    SpendMoney(cost);
    return true;
}

/// <summary>
/// 所持金とデッキの最低枚数を確認し、指定の球をショップで有償除去する。
/// </summary>
bool RunController::RemoveShopBall(int ballIndex, int cost)
{
    cost = (std::max)(0, cost);
    if (m_Impl->status.money < cost ||
        m_Impl->deck.GetRewardTargetCount() <= PlayerDeck::MinimumDeckSize ||
        !m_Impl->deck.RemoveRewardTarget(ballIndex))
    {
        return false;
    }

    SpendMoney(cost);
    return true;
}

/// <summary>
/// 戦闘報酬額と受取済みフラグを初期化する。
/// </summary>
void RunController::BeginStageReward()
{
    m_Impl->currentStageRewardMoney = 0;
    m_Impl->isStageRewardCollected = false;
}

/// <summary>
/// 戦闘報酬を未受取の場合に一度だけ所持金へ反映する。
/// </summary>
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

/// <summary>
/// 現在のステージで記録した報酬額を返す。
/// </summary>
int RunController::GetCurrentStageRewardMoney() const
{
    return m_Impl->currentStageRewardMoney;
}

/// <summary>
/// 現在のステージ報酬が受取済みかを返す。
/// </summary>
bool RunController::IsStageRewardCollected() const
{
    return m_Impl->isStageRewardCollected;
}

/// <summary>
/// 指定した種類に対応するレリックを所持しているかを判定する。
/// </summary>
bool RunController::HasRelic(RelicType type) const
{
    const std::size_t index = static_cast<std::size_t>(type);
    return index < m_Impl->ownedRelics.size() &&
        m_Impl->ownedRelics[index];
}

/// <summary>
/// 所持済みのレリックの種類数を数える。
/// </summary>
int RunController::GetOwnedRelicCount() const
{
    return static_cast<int>(std::count(
        m_Impl->ownedRelics.begin(),
        m_Impl->ownedRelics.end(),
        true));
}

/// <summary>
/// 指定のレリックを検証して所持状態に加える。
/// </summary>
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

/// <summary>
/// 指定のレリックの代金を確認し、購入できる場合は所持状態に追加する。
/// </summary>
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

    SpendMoney(cost);
    m_Impl->ownedRelics[static_cast<std::size_t>(relic.type)] = true;
    return true;
}

/// <summary>
/// 入手条件と所持状況を反映して、レリック候補を抽選する。
/// </summary>
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

/// <summary>
/// レリック抽選に使う乱数生成器のシードを設定する。
/// </summary>
void RunController::SeedRelics(std::uint32_t seed)
{
    m_Impl->relicRandomEngine.seed(seed);
}

/// <summary>
/// レリック抽選用の乱数生成器への変更可能な参照を返す。
/// </summary>
std::mt19937& RunController::RelicRandomEngine()
{
    return m_Impl->relicRandomEngine;
}

/// <summary>
/// レリック抽選用の乱数生成器を読み取り専用の参照で返す。
/// </summary>
const std::mt19937& RunController::RelicRandomEngine() const
{
    return m_Impl->relicRandomEngine;
}

/// <summary>
/// ショップのレリック候補一覧への変更可能な参照を返す。
/// </summary>
std::vector<int>& RunController::ShopRelicOffers()
{
    return m_Impl->shopRelicOffers;
}

/// <summary>
/// ショップのレリック候補一覧を読み取り専用の参照で返す。
/// </summary>
const std::vector<int>& RunController::ShopRelicOffers() const
{
    return m_Impl->shopRelicOffers;
}

/// <summary>
/// ショップの球候補一覧への変更可能な参照を返す。
/// </summary>
std::vector<int>& RunController::ShopBallOffers()
{
    return m_Impl->shopBallOffers;
}

/// <summary>
/// ショップの球候補一覧を読み取り専用の参照で返す。
/// </summary>
const std::vector<int>& RunController::ShopBallOffers() const
{
    return m_Impl->shopBallOffers;
}

/// <summary>
/// 中ボス戦のレリック候補一覧への変更可能な参照を返す。
/// </summary>
std::vector<int>& RunController::MidBossRelicOffers()
{
    return m_Impl->midBossRelicOffers;
}

/// <summary>
/// 中ボス戦のレリック候補一覧を読み取り専用の参照で返す。
/// </summary>
const std::vector<int>& RunController::MidBossRelicOffers() const
{
    return m_Impl->midBossRelicOffers;
}
