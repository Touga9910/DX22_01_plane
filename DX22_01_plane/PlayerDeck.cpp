#include "PlayerDeck.h"

#include <algorithm>
#include <random>

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

void PlayerDeck::SetCatalog(const std::vector<PlayerBallData>& catalog)
{
    m_Catalog = catalog;
    for (PlayerBallData& ball : m_Catalog)
    {
        ball.instanceId = 0;
        ball.status = NormalizeStatus(ball.status);
    }
}

void PlayerDeck::Reset()
{
    // 現在ボール・提示・保持・山札・捨て札をすべて回収する。
    // 実行中に強化された能力値を維持するため、ランタイムのデータを優先する。
    std::vector<PlayerBallData> rebuiltDeck;
    rebuiltDeck.reserve(
        m_DrawPile.size() +
        m_DiscardPile.size() +
        m_OfferedBalls.size() +
        (m_HeldBall.has_value() ? 1u : 0u) +
        (m_CurrentBall.has_value() ? 1u : 0u));

    if (m_CurrentBall.has_value())
    {
        rebuiltDeck.push_back(*m_CurrentBall);
    }

    if (m_HeldBall.has_value())
    {
        rebuiltDeck.push_back(*m_HeldBall);
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

    // 初回起動時など、ランタイムのデッキが空なら設定デッキを使用する。
    if (rebuiltDeck.empty())
    {
        rebuiltDeck = m_DefaultDeck;
    }

    m_DrawPile = std::move(rebuiltDeck);
    m_DiscardPile.clear();
    m_OfferedBalls.clear();
    m_HeldBall.reset();
    m_CurrentBall.reset();
    m_PreviousHeldOfferIndex = -1;
    m_IsCurrentBallUsed = false;

    ShuffleDrawPile();
}

void PlayerDeck::ResetToDefault()
{
    m_DrawPile = m_DefaultDeck;
    m_DiscardPile.clear();
    m_OfferedBalls.clear();
    m_HeldBall.reset();
    m_CurrentBall.reset();
    m_PreviousHeldOfferIndex = -1;
    m_IsCurrentBallUsed = false;
    ShuffleDrawPile();
}

bool PlayerDeck::PrepareOffer()
{
    if (m_CurrentBall.has_value())
    {
        return false;
    }

    if (!m_OfferedBalls.empty())
    {
        return true;
    }

    m_PreviousHeldOfferIndex = -1;

    // 保持中のボールは先頭の選択肢として提示する。
    if (m_HeldBall.has_value())
    {
        m_PreviousHeldOfferIndex = 0;
        m_OfferedBalls.push_back(std::move(*m_HeldBall));
        m_HeldBall.reset();
    }

    constexpr int OFFER_SIZE = 3;
    while (static_cast<int>(m_OfferedBalls.size()) < OFFER_SIZE)
    {
        PlayerBallData drawnBall;
        if (!DrawOneFromPile(drawnBall))
        {
            break;
        }

        drawnBall.status = NormalizeStatus(drawnBall.status);
        m_OfferedBalls.push_back(std::move(drawnBall));
    }

    return !m_OfferedBalls.empty();
}

bool PlayerDeck::SelectOffer(int selectedIndex, int heldIndex)
{
    if (selectedIndex < 0 ||
        selectedIndex >= static_cast<int>(m_OfferedBalls.size()))
    {
        return false;
    }

    if (heldIndex == selectedIndex ||
        heldIndex < 0 ||
        heldIndex >= static_cast<int>(m_OfferedBalls.size()))
    {
        heldIndex = -1;
    }

    m_CurrentBall = std::move(m_OfferedBalls[selectedIndex]);
    m_CurrentBall->status = NormalizeStatus(m_CurrentBall->status);

    m_HeldBall.reset();
    if (heldIndex >= 0)
    {
        m_HeldBall = std::move(m_OfferedBalls[heldIndex]);
        m_HeldBall->status = NormalizeStatus(m_HeldBall->status);
    }

    // 選択・保持されなかったボールは捨て札へ送る。
    for (int index = 0;
        index < static_cast<int>(m_OfferedBalls.size());
        index++)
    {
        if (index == selectedIndex || index == heldIndex)
        {
            continue;
        }

        m_DiscardPile.push_back(std::move(m_OfferedBalls[index]));
    }

    m_OfferedBalls.clear();
    m_PreviousHeldOfferIndex = -1;
    m_IsCurrentBallUsed = false;

    return true;
}

bool PlayerDeck::DrawNext()
{
    PlayerBallData drawnBall;
    if (!DrawOneFromPile(drawnBall))
    {
        m_CurrentBall.reset();
        m_IsCurrentBallUsed = false;
        return false;
    }

    m_CurrentBall = std::move(drawnBall);
    m_CurrentBall->status = NormalizeStatus(m_CurrentBall->status); // ステータス値を安全な範囲に補正
    m_IsCurrentBallUsed = false;                                  // 引いた直後は未使用扱い

    return true;
}

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

void PlayerDeck::MarkCurrentUsed()
{
    // 現在ボールがある場合のみ、使用済みとして記録する
    if (m_CurrentBall.has_value())
    {
        m_IsCurrentBallUsed = true;
    }
}

void PlayerDeck::ClearCurrentUsed()
{
    // 現在ボールの使用済みフラグを解除する
    m_IsCurrentBallUsed = false;
}

const PlayerBallData* PlayerDeck::GetCurrent() const
{
    // 現在ボールが無い場合はnullptrを返す
    if (!m_CurrentBall.has_value())
    {
        return nullptr;
    }

    return &(*m_CurrentBall);
}

PlayerBallData* PlayerDeck::GetCurrent()
{
    // 現在ボールが無い場合はnullptrを返す
    if (!m_CurrentBall.has_value())
    {
        return nullptr;
    }

    return &(*m_CurrentBall);
}

const PlayerBallData* PlayerDeck::GetOffer(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_OfferedBalls.size()))
    {
        return nullptr;
    }

    return &m_OfferedBalls[index];
}

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

    if (m_HeldBall.has_value())
    {
        count++;
    }

    return count;
}

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

    // 次に保持中のボールを参照する
    if (m_HeldBall.has_value())
    {
        if (index == 0)
        {
            return &(*m_HeldBall);
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

PlayerBallData* PlayerDeck::GetRewardTarget(int index)
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

    // 次に保持中のボールを参照する
    if (m_HeldBall.has_value())
    {
        if (index == 0)
        {
            return &(*m_HeldBall);
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

int PlayerDeck::GetCatalogCount() const
{
    return static_cast<int>(m_Catalog.size());
}

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

bool PlayerDeck::AddCatalogBall(int index)
{
    const PlayerBallData* catalogBall = GetCatalogBall(index);
    if (catalogBall == nullptr)
    {
        return false;
    }

    PlayerBallData addedBall = *catalogBall;
    addedBall.instanceId = m_NextInstanceId++;
    addedBall.status = NormalizeStatus(addedBall.status);
    m_DrawPile.push_back(std::move(addedBall));
    return true;
}

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

    if (m_HeldBall.has_value())
    {
        if (index == 0)
        {
            m_HeldBall.reset();
            return true;
        }
        index--;
    }

    if (index < static_cast<int>(m_OfferedBalls.size()))
    {
        m_OfferedBalls.erase(m_OfferedBalls.begin() + index);
        m_PreviousHeldOfferIndex = -1;
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

bool PlayerDeck::DrawOneFromPile(PlayerBallData& result)
{
    // 山札が空になった時点で、捨て札だけをシャッフルして補充する。
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

void PlayerDeck::ShuffleDrawPile()
{
    // 毎回同じ順番にならないように、乱数で山札をシャッフルする
    static std::mt19937 rng(std::random_device{}());
    std::shuffle(m_DrawPile.begin(), m_DrawPile.end(), rng);
}

BallStatus PlayerDeck::NormalizeStatus(BallStatus status)
{
    // 不正な値でゲーム処理が壊れないように、ステータスを安全な範囲へ補正する
    status.maxHp = (std::max)(1, status.maxHp);                 // HPは最低1
    status.mass = (std::max)(0.0001f, status.mass);            // 質量は0にしない
    status.radius = (std::max)(0.0f, status.radius);             // 半径は負数にしない
    status.restitution = std::clamp(status.restitution, 0.0f, 1.0f);  // 反発係数は0〜1に制限
    status.friction = (std::max)(0.0f, status.friction);           // 摩擦は負数にしない

    return status;
}
