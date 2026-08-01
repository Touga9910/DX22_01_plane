#pragma once

#include <string>
#include <vector>
#include "StageData.h"

class StageDataLoader
{
public:
    static std::vector<EnemyData> LoadEnemyDefinitions(
        const std::string& enemyMasterFilePath
    );

    static std::vector<StageData> LoadAll(
        const std::string& stageFilePath,
        const std::string& enemyMasterFilePath
    );

    static const StageData* FindById(
        const std::vector<StageData>& stages,
        const std::string& stageId
    );
};
