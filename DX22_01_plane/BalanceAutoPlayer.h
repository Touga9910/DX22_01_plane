#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

class Game;
class StageSelectScene;
struct PlayerBallData;

// バランス検証用の自動プレイヤー。
// Gameの進行を所有せず、通常プレイヤーと同じGame操作へ判断を出す。
class BalanceAutoPlayer final
{
public:
    void LoadConfig(
        const std::string& filePath =
            "assets/data/balance_autoplay.json");

    bool Update(Game& game);

    /// <summary>
    /// 自動プレイが有効かを返す。
    /// </summary>
    bool IsEnabled() const { return m_Enabled; }
    void SetEnabled(bool enabled);

    /// <summary>
    /// 現在のランを終えた時点で自動プレイを停止する要求があるかを返す。
    /// </summary>
    bool IsStopAfterCurrentRunRequested() const
    {
        return m_StopAfterCurrentRunRequested;
    }

    /// <summary>
    /// 現在のランの終了時に自動プレイを停止するかを設定する。
    /// </summary>
    void SetStopAfterCurrentRunRequested(bool requested)
    {
        m_StopAfterCurrentRunRequested = requested;
    }

    /// <summary>
    /// ラン終了時に自動プレイを停止する既定設定を返す。
    /// </summary>
    bool IsStopAfterCurrentRunDefault() const
    {
        return m_StopAfterCurrentRunDefault;
    }

    /// <summary>
    /// 自動プレイで記録しているラン数を返す。
    /// </summary>
    int GetRunCount() const { return m_RunCount; }
    /// <summary>
    /// 自動プレイに設定されたラン数の上限を返す。
    /// </summary>
    int GetMaximumRuns() const { return m_MaximumRuns; }
    /// <summary>
    /// 自動プレイ用に保持している乱数シードを返す。
    /// </summary>
    unsigned int GetRandomSeed() const { return m_RandomSeed; }

    /// <summary>
    /// 指定したシードで自動プレイ用の乱数生成器を初期化する。
    /// </summary>
    void SeedRandom(std::uint32_t seed)
    {
        m_RandomEngine.seed(seed);
    }

    /// <summary>
    /// 後で調整する球の一覧を消去する。
    /// </summary>
    void ResetPendingBallAdjustments()
    {
        m_PendingBallAdjustments.clear();
    }

    /// <summary>
    /// 後で調整する球のインスタンスID一覧を返す。
    /// </summary>
    const std::vector<std::uint64_t>& GetPendingBallAdjustments() const
    {
        return m_PendingBallAdjustments;
    }

    bool IsBallAdjustmentCandidate(std::uint64_t instanceId) const;
    bool HasAvailableRestBenefit(const Game& game) const;
    void NotifyFullHpEnemySurvived(const PlayerBallData* currentBall);
    void RemovePendingBall(std::uint64_t instanceId);
    void PrunePendingBalls(const Game& game);

private:
    void ApplyRouteChoice(Game& game, StageSelectScene* stageSelect);
    void ApplyRestSiteAction(Game& game);
    void ApplyShopAction(Game& game);
    void SelectBall(Game& game);
    bool FireShot(Game& game);
    void ApplyReward(Game& game);
    bool IsHealNeeded(const Game& game) const;
    int FindRelicToBuy(const Game& game) const;
    int FindWeakestBall(const Game& game) const;
    int FindMissingCatalogBall(const Game& game) const;
    int FindMissingClearRewardBallOffer(const Game& game) const;
    int FindUpgradeTarget(const Game& game) const;
    bool HasShopAction(const Game& game) const;
    int FindPendingUpgradeableBall(const Game& game) const;
    int FindPendingRemovalBall(const Game& game) const;

private:
    bool m_Enabled = false;
    bool m_RestartAfterGameOver = true;
    bool m_StopAfterCurrentRunDefault = false;
    bool m_StopAfterCurrentRunRequested = false;
    int m_DecisionDelayFrames = 20;
    int m_DecisionFrame = 0;
    int m_RunCount = 0;
    int m_MaximumRuns = 0;
    float m_MinimumShotPower = 4.0f;
    float m_MaximumShotPower = 8.0f;
    float m_AimJitterDegrees = 1.5f;
    std::mt19937 m_RandomEngine{ std::random_device{}() };
    unsigned int m_RandomSeed = 20260727u;
    std::vector<std::uint64_t> m_PendingBallAdjustments;
};
