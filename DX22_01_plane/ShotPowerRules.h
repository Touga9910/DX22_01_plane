#pragma once
#include <algorithm>
namespace ShotPowerRules
{
    inline constexpr float Minimum=1.0f;
    inline constexpr float Maximum=8.0f;
    constexpr float AtFraction(float ratio)
    { return std::clamp(Maximum*ratio,Minimum,Maximum); }
}
