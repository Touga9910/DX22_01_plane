#pragma once
#include <algorithm>

namespace PlayerShieldRules
{
    // Runtime and shot prediction consume shield before changing player HP.
    /// <summary>
    /// シールドを消費してダメージを吸収し、HPに適用する残りのダメージを返す。
    /// </summary>
    inline int Absorb(int& shield, int damage)
    {
        const int incoming = (std::max)(0, damage);
        shield = (std::max)(0, shield);
        const int absorbed = (std::min)(shield, incoming);
        shield -= absorbed;
        return incoming - absorbed;
    }
}
