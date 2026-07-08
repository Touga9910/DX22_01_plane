#pragma once

#include <string>
#include "StageData.h"

class StageDataLoader
{
public:
    static StageData Load(
        const std::string& stageFilePath,
        const std::string& enemyMasterFilePath
    );
};