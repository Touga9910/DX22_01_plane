#pragma once

#include "PlayerBallData.h"

#include <cstdint>
#include <optional>
#include <random>
#include <vector>

struct PlayerDeckSnapshot
{
    std::vector<PlayerBallData> drawPile;
    std::vector<PlayerBallData> discardPile;
    std::vector<PlayerBallData> offeredBalls;
    // Legacy save migration only. New snapshots always write nullopt.
    std::optional<PlayerBallData> heldBall;
    std::optional<PlayerBallData> currentBall;
    int previousHeldOfferIndex = -1;
    bool currentBallUsed = false;
    std::uint64_t nextInstanceId = 1;
    std::mt19937 randomEngine;
};

class PlayerDeck
{
public:
    static constexpr int MinimumDeckSize = 5;

    // -------------------------
    // デッキ初期化
    // -------------------------
    void SetDefaultDeck(const std::vector<PlayerBallData>& defaultDeck);
    void SetCatalog(const std::vector<PlayerBallData>& catalog);
    void Seed(std::uint32_t seed);
    /// <summary>
    /// 球の抽選に使う乱数生成器への変更可能な参照を返す。
    /// </summary>
    std::mt19937& RandomEngine() { return m_RandomEngine; }
    /// <summary>
    /// 球の抽選に使う乱数生成器を読み取り専用の参照で返す。
    /// </summary>
    const std::mt19937& RandomEngine() const { return m_RandomEngine; }
    void Reset();
    void ResetToDefault();

    // -------------------------
    // ボール提示・選択
    // -------------------------
    bool PrepareOffer(int offerSize = 5);
    // Second argument is ignored for source compatibility with older controllers.
    bool SelectOffer(int selectedIndex, int legacyHeldIndex = -1);

    /// <summary>
    /// 現在提示している球の候補数を返す。
    /// </summary>
    int GetOfferCount() const { return static_cast<int>(m_OfferedBalls.size()); }
    const PlayerBallData* GetOffer(int index) const;
    /// <summary>
    /// 旧形式の候補保持判定に対して、保持なしを表す既定値を返す。
    /// </summary>
    bool WasHeldOffer(int) const { return false; } // Legacy read API
    /// <summary>
    /// 旧形式の球保持判定に対して、保持球なしを表す既定値を返す。
    /// </summary>
    bool HasHeldBall() const { return false; } // Legacy read API

    // -------------------------
    // 山札・捨て札操作
    // -------------------------
    bool DiscardCurrentIfUsed();
    void MarkCurrentUsed();
    void ClearCurrentUsed();

    // -------------------------
    // 現在ボール
    // -------------------------
    /// <summary>
    /// 現在選択中の球があるかを判定する。
    /// </summary>
    bool HasCurrent() const { return m_CurrentBall.has_value(); }
    /// <summary>
    /// 現在の球がショットで使用済みかを返す。
    /// </summary>
    bool IsCurrentUsed() const { return m_IsCurrentBallUsed; }

    const PlayerBallData* GetCurrent() const;
    PlayerBallData* GetCurrent();
    // -------------------------
    // 山札・捨て札情報
    // -------------------------
    /// <summary>
    /// 山札に残っている球の数を返す。
    /// </summary>
    int GetDrawPileCount() const { return static_cast<int>(m_DrawPile.size()); }
    /// <summary>
    /// 捨て札にある球の数を返す。
    /// </summary>
    int GetDiscardPileCount() const { return static_cast<int>(m_DiscardPile.size()); }

    /// <summary>
    /// 現在の山札を読み取り専用の参照で返す。
    /// </summary>
    const std::vector<PlayerBallData>& GetDrawPile() const { return m_DrawPile; }
    /// <summary>
    /// 現在の捨て札を読み取り専用の参照で返す。
    /// </summary>
    const std::vector<PlayerBallData>& GetDiscardPile() const { return m_DiscardPile; }

    // -------------------------
    // 報酬対象
    // -------------------------
    int GetRewardTargetCount() const;
    const PlayerBallData* GetRewardTarget(int index) const;
    PlayerBallData* GetRewardTarget(int index);

    // -------------------------
    // 休憩所・ショップ操作
    // -------------------------
    int GetCatalogCount() const;
    const PlayerBallData* GetCatalogBall(int index) const;
    std::vector<int> RollCatalogOfferIndices(int offerSize = 3);
    std::vector<int> RollDistinctCatalogOfferIndices(int offerSize = 3);
    bool AddCatalogBall(int index);
    bool RemoveRewardTarget(int index);

    // Save I/O receives a typed copy instead of privileged field access.
    PlayerDeckSnapshot CaptureSnapshot() const;
    bool RestoreSnapshot(PlayerDeckSnapshot snapshot);

private:
    bool DrawOneFromPile(PlayerBallData& result);
    void ShuffleDrawPile();

private:
    std::vector<PlayerBallData> m_DefaultDeck;      // 初期デッキ
    std::vector<PlayerBallData> m_Catalog;          // ショップで購入できるボール定義
    std::vector<PlayerBallData> m_DrawPile;         // 山札
    std::vector<PlayerBallData> m_DiscardPile;      // 捨て札
    std::vector<PlayerBallData> m_OfferedBalls;     // 今回提示しているボール
    std::optional<PlayerBallData> m_CurrentBall;    // 現在使用中のボール
    bool m_IsCurrentBallUsed = false;               // 現在ボールを使用済みかどうか
    std::uint64_t m_NextInstanceId = 1;              // 同名ボールを区別するランタイムID
    std::mt19937 m_RandomEngine{ std::random_device{}() };
};
