#include "../BallStatusJson.h"
#include "../PlayerBallDataLoader.h"
#include "../StageDataLoader.h"

#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>

int main()
{
    // Legal JSON doubles can overflow the float representation without a
    // parse exception. Exercise both real data loaders, not only a synthetic NaN.
    const char* playerPath = "float_overflow_player_status.json";
    std::ofstream(playerPath) << R"({"status":{"mass":1e100,"radius":1e100,"friction":1e100}})";
    const auto player = PlayerBallDataLoader::Load(playerPath, {}, {});
    assert(IsValidPlayerBallStatus(player.defaultBallStatus));
    assert(player.defaultBallStatus.mass == BallStatus{}.mass);
    assert(player.defaultBallStatus.radius == BallStatus{}.radius);
    assert(player.defaultBallStatus.friction == BallStatus{}.friction);

    const char* enemyPath = "float_overflow_enemy_master.json";
    std::ofstream(enemyPath) << R"({"enemies":[{"id":"overflow_enemy","status":{"mass":1e100,"radius":1e100,"friction":1e100}}]})";
    const auto enemies = StageDataLoader::LoadEnemyDefinitions(enemyPath);
    assert(enemies.size() == 1);
    assert(IsValidBallStatus(enemies.front().status));

    BallStatus invalid;
    invalid.mass = std::numeric_limits<float>::quiet_NaN();
    invalid.restitution = std::numeric_limits<float>::quiet_NaN();
    invalid.pierceSpeedRetention = std::numeric_limits<float>::quiet_NaN();
    invalid.chainImpactRadius = std::numeric_limits<float>::quiet_NaN();
    invalid.anchorStopSpeedSquared = std::numeric_limits<float>::quiet_NaN();
    assert(IsValidBallStatus(NormalizeBallStatus(invalid)));

    // Normal authored profiles and supported finite edge values are unchanged.
    BallStatus normal;
    normal.mass = 15.63f;
    normal.radius = 6.0f;
    normal.friction = 1.0f;
    assert(WriteBallStatus(NormalizeBallStatus(normal)) == WriteBallStatus(normal));
    std::cout << "ball status safety regressions passed\n";
}
