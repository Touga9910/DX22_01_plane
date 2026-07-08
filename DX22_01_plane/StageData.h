#pragma once

#include <string>
#include <vector>
#include "EnemyData.h"

struct StageData
{
    std::string stageId = "stage_default";
    int par = 4;

    std::vector<EnemyData> enemies;
};