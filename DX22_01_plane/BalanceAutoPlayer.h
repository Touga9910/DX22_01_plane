#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

class Game;
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

    bool IsEnabled() const { return m_Enabled; }
    void SetEnabled(bool enabled);

    bool IsStopAfterCurrentRunRequested() const
    {
        return m_StopAfterCurrentRunRequested;
    }

    void SetStopAfterCurrentRunRequested(bool requested)
    {
        m_StopAfterCurrentRunRequested = requested;
    }

    bool IsStopAfterCurrentRunDefault() const
    {
        return m_StopAfterCurrentRunDefault;
    }

    int GetRunCount() const { return m_RunCount; }
    int GetMaximumRuns() const { return m_MaximumRuns; }
    unsigned int GetRandomSeed() const { return m_RandomSeed; }

    void SeedRandom(std::uint32_t seed)
    {
        m_RandomEngine.seed(seed);
    }

    void ResetPendingBallAdjustments()
    {
        m_PendingBallAdjustments.clear();
    }

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
