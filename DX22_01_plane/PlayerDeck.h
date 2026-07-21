#pragma once

#include "PlayerBallData.h"

#include <optional>
#include <vector>

class PlayerDeck
{
public:
    // -------------------------
    // デッキ初期化
    // -------------------------
    void SetDefaultDeck(const std::vector<PlayerBallData>& defaultDeck);
    void Reset();

    // -------------------------
    // ボール提示・選択・保持
    // -------------------------
    bool PrepareOffer();
    bool SelectOffer(int selectedIndex, int heldIndex);

    int GetOfferCount() const { return static_cast<int>(m_OfferedBalls.size()); }
    const PlayerBallData* GetOffer(int index) const;
    bool WasHeldOffer(int index) const { return index == m_PreviousHeldOfferIndex; }
    bool HasHeldBall() const { return m_HeldBall.has_value(); }

    // -------------------------
    // 山札・捨て札操作
    // -------------------------
    bool DrawNext();
    bool DiscardCurrentIfUsed();
    void MarkCurrentUsed();
    void ClearCurrentUsed();

    // -------------------------
    // 現在ボール
    // -------------------------
    bool HasCurrent() const { return m_CurrentBall.has_value(); }
    bool IsCurrentUsed() const { return m_IsCurrentBallUsed; }

    const PlayerBallData* GetCurrent() const;
    PlayerBallData* GetCurrent();
    // -------------------------
    // 山札・捨て札情報
    // -------------------------
    int GetDrawPileCount() const { return static_cast<int>(m_DrawPile.size()); }
    int GetDiscardPileCount() const { return static_cast<int>(m_DiscardPile.size()); }

    const std::vector<PlayerBallData>& GetDrawPile() const { return m_DrawPile; }
    const std::vector<PlayerBallData>& GetDiscardPile() const { return m_DiscardPile; }

    // -------------------------
    // 報酬対象
    // -------------------------
    int GetRewardTargetCount() const;
    const PlayerBallData* GetRewardTarget(int index) const;
    PlayerBallData* GetRewardTarget(int index);

private:
    bool DrawOneFromPile(PlayerBallData& result);
    void ShuffleDrawPile();
    static BallStatus NormalizeStatus(BallStatus status);

private:
    std::vector<PlayerBallData> m_DefaultDeck;      // 初期デッキ
    std::vector<PlayerBallData> m_DrawPile;         // 山札
    std::vector<PlayerBallData> m_DiscardPile;      // 捨て札
    std::vector<PlayerBallData> m_OfferedBalls;     // 今回提示しているボール
    std::optional<PlayerBallData> m_HeldBall;       // 次回まで保持するボール
    std::optional<PlayerBallData> m_CurrentBall;    // 現在使用中のボール
    int m_PreviousHeldOfferIndex = -1;              // 提示内で前回から保持されていた位置
    bool m_IsCurrentBallUsed = false;               // 現在ボールを使用済みかどうか
};
