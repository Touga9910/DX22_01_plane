#include "../ShotPowerRules.h"
#include "../CollisionPresentationRules.h"
#include <cassert>
#include <cmath>
#include <iostream>
int main()
{
    static_assert(ShotPowerRules::Maximum==8);
    static_assert(ShotPowerRules::AtFraction(.8f)==6.4f);
    assert(ShotPowerRules::AtFraction(1)==ShotPowerRules::Maximum);
    assert(ShotPowerRules::AtFraction(-1)==ShotPowerRules::Minimum);
    assert(ShotPowerRules::AtFraction(2)==ShotPowerRules::Maximum);
    using Rules=CollisionPresentationRules;
    const auto shake=Rules::ImpactShake(Rules::Strength::Strong,false);
    assert(shake.lifetime==.16f&&shake.strength==.88f);
    assert(Rules::ImpactShake(Rules::Strength::Medium,true).strength==.30f);
    assert(Rules::ImpactShake(Rules::Strength::Weak,false).lifetime==0);
    const auto end=Rules::ShakeOffset(1,0,shake.strength);assert(end.x==0&&end.z==0);
    std::cout<<"PASS: shared shot limits, 80 percent = 6.4, clamping\n";
}
