#pragma once

struct PlayerBallData;

// Shared policy values. State and RNG remain owned by BalanceAutoPlayer.
namespace BalanceAutoPolicy
{
    inline constexpr int kExtraRewardMoney = 10;
    inline constexpr int kAutoShopRemoveCost = 15;
    inline constexpr int kAutoShopBallCost = 20;
    inline constexpr int kAutoMaximumDeckSize = 8;

    float GetAutoBallValue(const PlayerBallData& ball);
}
