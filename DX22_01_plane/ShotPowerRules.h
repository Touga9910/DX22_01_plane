#pragma once
#include <algorithm>
namespace ShotPowerRules
{
    inline constexpr float Minimum=1.0f;
    inline constexpr float Maximum=8.0f;
    /// <summary>
    /// 最大パワーに指定割合を掛け、発射可能なパワー範囲に制限する。
    /// </summary>
    constexpr float AtFraction(float ratio)
    { return std::clamp(Maximum*ratio,Minimum,Maximum); }
}
