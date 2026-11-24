#include "../PlayerShieldRules.h"
#include <cassert>
#include <iostream>
/// <summary>
/// シールドによるポケットや敵攻撃の軽減と、後から得たシールドの適用順を検証する。
/// </summary>
int main()
{
    for (int shieldAtShot : {0, 3, 4, 5})
    {
        int shield = shieldAtShot;
        int hp = 47;
        hp -= PlayerShieldRules::Absorb(shield, 2);
        assert(hp == (shieldAtShot == 0 ? 45 : 47));
        assert(shield == (shieldAtShot > 2 ? shieldAtShot - 2 : 0));
        // The same shield cannot protect against pocket and enemy attacks twice.
        hp -= PlayerShieldRules::Absorb(shield, 5);
        assert(hp == 47 - (7 - shieldAtShot));
    }
    int shield = 3;
    assert(PlayerShieldRules::Absorb(shield, 0) == 0 && shield == 3);
    assert(PlayerShieldRules::Absorb(shield, -1) == 0 && shield == 3);
    assert(PlayerShieldRules::Absorb(shield, 5) == 2 && shield == 0);
    shield += 1; // Guard+2 enemy collision after pocket: protects future hits only.
    assert(PlayerShieldRules::Absorb(shield, 2) == 1 && shield == 0);
    std::cout << "PASS: Guard levels, pocket then enemy attack, partial shield, late grants\n";
}
