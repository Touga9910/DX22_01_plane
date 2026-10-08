#pragma once

#include "StageData.h"

#include <optional>
#include <string>

class PlayerBall;
class BattleSceneServices;
class TableFrame;

struct BattleSceneSetupResult
{
    std::optional<StageData> stage;
    std::string selectedStageId;
    std::string stageJsonPath;
    std::string enemyJsonPath;
};

class BattleSceneSetup
{
public:
    explicit BattleSceneSetup(BattleSceneServices& services)
        : m_Services(services)
    {
    }

    BattleSceneSetupResult Prepare() const;
    void ValidateEnemySpawns(
        const StageData& stage,
        const PlayerBall& player,
        const TableFrame& tableFrame) const;

private:
    void ArrangeDenseEnemySpawns(StageData& stage) const;

    BattleSceneServices& m_Services;
    std::string m_StageJsonPath = "assets/data/stage_01.json";
    std::string m_EnemyJsonPath = "assets/data/enemy_data.json";
};
