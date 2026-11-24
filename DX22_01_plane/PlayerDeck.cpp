#include "PlayerDeck.h"

#include <algorithm>
#include <random>
#include <utility>

/// <summary>
/// 既定のデッキを保持し、収録する球にインスタンスIDを割り当てる。
/// </summary>
void PlayerDeck::SetDefaultDeck(const std::vector<PlayerBallData>& defaultDeck)
{
    // リセット時に戻す基準デッキを保存する
    m_DefaultDeck = defaultDeck;
    m_NextInstanceId = 1;
    for (PlayerBallData& ball : m_DefaultDeck)
    {
        ball.instanceId = m_NextInstanceId++;
    }
}

/// <summary>
/// カタログの定義を保持し、各球の初期ステータスを正規化する。
/// </summary>
void PlayerDeck::SetCatalog(const std::vector<PlayerBallData>& catalog)
{
    m_Catalog = catalog;
    for (PlayerBallData& ball : m_Catalog)
    {
        ball.instanceId = 0;
        ball.status = NormalizeBallStatus(ball.status);
    }
}

/// <summary>
/// 球の抽選に使う乱数生成器のシードを設定する。
/// </summary>
void PlayerDeck::Seed(std::uint32_t seed)
{
    m_RandomEngine.seed(seed);
}

/// <summary>
/// 山札、捨て札、提示候補、選択中の球を集めてデッキを組み直す。
/// </summary>
void PlayerDeck::Reset()
{
    // 現在ボール・手札・山札・捨て札をすべて回収
    // 
    // 
    // 
    // 実行中に強化された能力値を維持するため、ランタイムのデータを優先
    std::vector<PlayerBallData> rebuiltDeck;
    rebuiltDeck.reserve(
        m_DrawPile.size() +
        m_DiscardPile.size() +
        m_OfferedBalls.size() +
        (m_CurrentBall.has_value() ? 1u : 0u));

    if (m_CurrentBall.has_value())
    {
        rebuiltDeck.push_back(*m_CurrentBall);
    }

    rebuiltDeck.insert(
        rebuiltDeck.end(),
        m_OfferedBalls.begin(),
        m_OfferedBalls.end());

    rebuiltDeck.insert(
        rebuiltDeck.end(),
        m_DrawPile.begin(),
        m_DrawPile.end());
    rebuiltDeck.insert(
        rebuiltDeck.end(),
        m_DiscardPile.begin(),
        m_DiscardPile.end());

    // 初回起動時など、ランタイムのデッキが空なら設定デッキを使用
    if (rebuiltDeck.empty())
    {
        rebuiltDeck = m_DefaultDeck;
    }

    m_DrawPile = std::move(rebuiltDeck);
    m_DiscardPile.clear();
    m_OfferedBalls.clear();
    m_CurrentBall.reset();
    m_IsCurrentBallUsed = false;

    ShuffleDrawPile();
}

/// <summary>
/// 現在の候補と選択状態をリセットし、既定のデッキから山札を作り直す。
/// </summary>
void PlayerDeck::ResetToDefault()
{
    m_DrawPile = m_DefaultDeck;
    m_DiscardPile.clear();
    m_OfferedBalls.clear();
    m_CurrentBall.reset();
    m_IsCurrentBallUsed = false;
    ShuffleDrawPile();
}

/// <summary>
/// 球の選択前で提示候補が空なら、山札から指定数まで候補を用意する。
/// </summary>
bool PlayerDeck::PrepareOffer(int offerSize)
{
    if (m_CurrentBall.has_value())
    {
        return false;
    }

    if (!m_OfferedBalls.empty())
    {
        return true;
    }

    offerSize = (std::max)(1, offerSize);
    while (static_cast<int>(m_OfferedBalls.size()) < offerSize)
    {
        PlayerBallData drawnBall;
        if (!DrawOneFromPile(drawnBall))
        {
            break;
        }

        drawnBall.status = NormalizeBallStatus(drawnBall.status);
        m_OfferedBalls.push_back(std::move(drawnBall));
    }

    return !m_OfferedBalls.empty();
}

/// <summary>
/// 指定した候補を現在の球に移し、選ばなかった候補を捨て札に送る。
/// </summary>
bool PlayerDeck::SelectOffer(int selectedIndex, int /*legacyHeldIndex*/)
{
    if (selectedIndex < 0 ||
        selectedIndex >= static_cast<int>(m_OfferedBalls.size()))
    {
        return false;
    }

    m_CurrentBall = std::move(m_OfferedBalls[selectedIndex]);
    m_CurrentBall->status = NormalizeBallStatus(m_CurrentBall->status);

    // 選択しなかった全カードは捨て札へ送る。
    for (int index = 0;
        index < static_cast<int>(m_OfferedBalls.size());
        index++)
    {
        if (index == selectedIndex)
        {
            continue;
        }

        m_DiscardPile.push_back(std::move(m_OfferedBalls[index]));
    }

    m_OfferedBalls.clear();
    m_IsCurrentBallUsed = false;

    return true;
}

/// <summary>
/// 選択中の球が使用済みなら、捨て札に移して選択を解除する。
/// </summary>
bool PlayerDeck::DiscardCurrentIfUsed()
{
    // 現在ボールが無い場合は捨て札に送れない
    if (!m_CurrentBall.has_value())
    {
        m_IsCurrentBallUsed = false;
        return false;
    }

    // まだ使用していない場合は捨て札に送らない
    if (!m_IsCurrentBallUsed)
    {
        return false;
    }

    // 使用済みの現在ボールを捨て札へ移す
    m_DiscardPile.push_back(std::move(*m_CurrentBall));
    m_CurrentBall.reset();
    m_IsCurrentBallUsed = false;

    return true;
}

/// <summary>
/// 現在の球があるときに、ショットで使用済みとして記録する。
/// </summary>
void PlayerDeck::MarkCurrentUsed()
{
    // 現在ボールがある場合のみ、使用済みとして記録する
    if (m_CurrentBall.has_value())
    {
        m_IsCurrentBallUsed = true;
    }
}

/// <summary>
/// 現在の球を使用済みとするフラグをクリアする。
/// </summary>
void PlayerDeck::ClearCurrentUsed()
{
    // 現在ボールの使用済みフラグを解除する
    m_IsCurrentBallUsed = false;
}

/// <summary>
/// 現在選択中の球への読み取り専用ポインターを返す。
/// </summary>
const PlayerBallData* PlayerDeck::GetCurrent() const
{
    // 現在ボールが無い場合はnullptrを返す
    if (!m_CurrentBall.has_value())
    {
        return nullptr;
    }

    return &(*m_CurrentBall);
}

/// <summary>
/// 現在選択中の球への変更可能なポインターを返す。
/// </summary>
PlayerBallData* PlayerDeck::GetCurrent()
{
    // 現在ボールが無い場合はnullptrを返す
    if (!m_CurrentBall.has_value())
    {
        return nullptr;
    }

    return &(*m_CurrentBall);
}

/// <summary>
/// 番号に対応する提示候補の球を返し、範囲外なら空を返す。
/// </summary>
const PlayerBallData* PlayerDeck::GetOffer(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_OfferedBalls.size()))
    {
        return nullptr;
    }

    return &m_OfferedBalls[index];
}

/// <summary>
/// 選択中の球と各札に分かれている球から、報酬操作の対象数を数える。
/// </summary>
int PlayerDeck::GetRewardTargetCount() const
{
    // どの場所にあるボールも報酬の強化対象に含める。
    int count = static_cast<int>(
        m_DrawPile.size() +
        m_DiscardPile.size() +
        m_OfferedBalls.size());

    if (m_CurrentBall.has_value())
    {
        count++;
    }

    return count;
}

/// <summary>
/// 選択中の球、山札、捨て札、提示候補から報酬対象の番号に対応する球を探す。
/// </summary>
const PlayerBallData* PlayerDeck::GetRewardTarget(int index) const
{
    // 範囲外の負数は無効扱いにする
    if (index < 0)
    {
        return nullptr;
    }

    // 先頭は現在ボールを報酬対象として扱う
    if (m_CurrentBall.has_value())
    {
        if (index == 0)
        {
            return &(*m_CurrentBall);
        }

        index--;
    }

    // 次に提示中のボールを参照する
    if (index < static_cast<int>(m_OfferedBalls.size()))
    {
        return &m_OfferedBalls[index];
    }

    index -= static_cast<int>(m_OfferedBalls.size());

    // 次に山札内のボールを参照する
    if (index < static_cast<int>(m_DrawPile.size()))
    {
        return &m_DrawPile[index];
    }

    // 最後に捨て札内のボールを参照する
    index -= static_cast<int>(m_DrawPile.size());
    if (index < static_cast<int>(m_DiscardPile.size()))
    {
        return &m_DiscardPile[index];
    }

    return nullptr;
}

/// <summary>
/// 報酬対象として指定した球に対する変更可能なポインターを返す。
/// </summary>
PlayerBallData* PlayerDeck::GetRewardTarget(int index)
{
    return const_cast<PlayerBallData*>(
        std::as_const(*this).GetRewardTarget(index));
}

/// <summary>
/// カタログに登録された球の定義数を返す。
/// </summary>
int PlayerDeck::GetCatalogCount() const
{
    return static_cast<int>(m_Catalog.size());
}

/// <summary>
/// カタログの指定位置にある球の定義を返す。
/// </summary>
const PlayerBallData* PlayerDeck::GetCatalogBall(int index) const
{
    if (index < 0)
    {
        return nullptr;
    }

    return index < static_cast<int>(m_Catalog.size())
        ? &m_Catalog[static_cast<std::size_t>(index)]
        : nullptr;
}

/// <summary>
/// カタログから提示に使う球の定義番号を抽選する。
/// </summary>
std::vector<int> PlayerDeck::RollCatalogOfferIndices(int offerSize)
{
    std::vector<int> result;
    if (offerSize <= 0 || m_Catalog.empty())
    {
        return result;
    }

    result.reserve(static_cast<std::size_t>(offerSize));
    std::uniform_int_distribution<int> distribution(
        0,
        static_cast<int>(m_Catalog.size()) - 1);
    for (int index = 0; index < offerSize; ++index)
    {
        // Each slot is sampled independently so the same ball may be offered
        // more than once, matching the intended with-replacement reward draw.
        result.push_back(distribution(m_RandomEngine));
    }
    return result;
}

/// <summary>
/// 同じ定義を重複させず、カタログから提示候補の番号を抽選する。
/// </summary>
std::vector<int> PlayerDeck::RollDistinctCatalogOfferIndices(int offerSize)
{
    const int count = (std::min)(
        (std::max)(0, offerSize),
        static_cast<int>(m_Catalog.size()));
    std::vector<int> result;
    result.reserve(m_Catalog.size());
    for (int catalogIndex = 0;
        catalogIndex < static_cast<int>(m_Catalog.size());
        ++catalogIndex)
    {
        result.push_back(catalogIndex);
    }
    std::shuffle(result.begin(), result.end(), m_RandomEngine);
    result.resize(static_cast<std::size_t>(count));
    return result;
}

/// <summary>
/// カタログの指定球を複製し、固有IDを割り当ててデッキに加える。
/// </summary>
bool PlayerDeck::AddCatalogBall(int index)
{
    const PlayerBallData* catalogBall = GetCatalogBall(index);
    if (catalogBall == nullptr)
    {
        return false;
    }

    PlayerBallData addedBall = *catalogBall;
    addedBall.instanceId = m_NextInstanceId++;
    addedBall.status = NormalizeBallStatus(addedBall.status);
    m_DrawPile.push_back(std::move(addedBall));
    return true;
}

/// <summary>
/// デッキの最低枚数を保ちながら、指定された報酬対象の球を取り除く。
/// </summary>
bool PlayerDeck::RemoveRewardTarget(int index)
{
    if (index < 0 ||
        GetRewardTargetCount() <= MinimumDeckSize)
    {
        return false;
    }

    if (m_CurrentBall.has_value())
    {
        if (index == 0)
        {
            m_CurrentBall.reset();
            m_IsCurrentBallUsed = false;
            return true;
        }
        index--;
    }

    if (index < static_cast<int>(m_OfferedBalls.size()))
    {
        m_OfferedBalls.erase(m_OfferedBalls.begin() + index);
        return true;
    }
    index -= static_cast<int>(m_OfferedBalls.size());

    if (index < static_cast<int>(m_DrawPile.size()))
    {
        m_DrawPile.erase(m_DrawPile.begin() + index);
        return true;
    }
    index -= static_cast<int>(m_DrawPile.size());

    if (index < static_cast<int>(m_DiscardPile.size()))
    {
        m_DiscardPile.erase(m_DiscardPile.begin() + index);
        return true;
    }

    return false;
}

/// <summary>
/// 全ての札と候補、現在の球、乱数生成器を保存用のスナップショットにまとめる。
/// </summary>
PlayerDeckSnapshot PlayerDeck::CaptureSnapshot() const
{
    return {
        m_DrawPile,
        m_DiscardPile,
        m_OfferedBalls,
        std::nullopt,
        m_CurrentBall,
        -1,
        m_IsCurrentBallUsed,
        m_NextInstanceId,
        m_RandomEngine,
    };
}

/// <summary>
/// スナップショットの候補番号とIDを検証し、デッキと乱数状態を復元する。
/// </summary>
bool PlayerDeck::RestoreSnapshot(PlayerDeckSnapshot snapshot)
{
    if (snapshot.previousHeldOfferIndex < -1 ||
        snapshot.previousHeldOfferIndex >=
            static_cast<int>(snapshot.offeredBalls.size()) ||
        snapshot.nextInstanceId == 0)
    {
        return false;
    }

    m_DrawPile = std::move(snapshot.drawPile);
    m_DiscardPile = std::move(snapshot.discardPile);
    m_OfferedBalls = std::move(snapshot.offeredBalls);
    if (snapshot.heldBall.has_value())
        m_DiscardPile.push_back(std::move(*snapshot.heldBall));
    m_CurrentBall = std::move(snapshot.currentBall);
    m_IsCurrentBallUsed = snapshot.currentBallUsed;
    m_NextInstanceId = snapshot.nextInstanceId;
    m_RandomEngine = snapshot.randomEngine;
    return true;
}

/// <summary>
/// 山札から球を1個取り出し、山札が空なら捨て札をシャッフルして補充する。
/// </summary>
bool PlayerDeck::DrawOneFromPile(PlayerBallData& result)
{
    // 山札が空になった時点で、捨て札だけをシャッフルして補充
    if (m_DrawPile.empty() && !m_DiscardPile.empty())
    {
        m_DrawPile = std::move(m_DiscardPile);
        m_DiscardPile.clear();
        ShuffleDrawPile();
    }

    if (m_DrawPile.empty())
    {
        return false;
    }

    result = std::move(m_DrawPile.back());
    m_DrawPile.pop_back();
    return true;
}

/// <summary>
/// 現在の乱数生成器を使って山札の順序をシャッフルする。
/// </summary>
void PlayerDeck::ShuffleDrawPile()
{
    std::shuffle(
        m_DrawPile.begin(),
        m_DrawPile.end(),
        m_RandomEngine);
}
