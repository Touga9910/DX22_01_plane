#pragma once

#include <string>
#include "utility.h"
#include "BallStatus.h"
#include "MeshRenderer.h"

struct EnemyData
{
    std::string id = "enemy_default";

    std::string modelFilePath = "assets/model/GolfBall/golf_ball.obj";
    std::string textureDirectory = "assets/model/GolfBall";

    BallStatus status;

    DirectX::SimpleMath::Vector3 initPosition =
        DirectX::SimpleMath::Vector3(50.0f, 0.0f, 50.0f);

    DirectX::SimpleMath::Vector3 scale =
        DirectX::SimpleMath::Vector3(2.4f, 2.4f, 2.4f);

    int rewardMoney = 0;
    int rewardExp = 0;
};
