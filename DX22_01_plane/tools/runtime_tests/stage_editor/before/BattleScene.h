#pragma once
#include "StageBase.h"

#include <string>
#include <filesystem>

struct StageData;
class PlayerBall;
class TableFrame;

class BattleScene : public StageBase
{
public:
    BattleScene();
    ~BattleScene() override = default;

    void Init();
    void Update() override;

private:
    std::string m_StageJsonPath = "assets/data/stage_01.json";
    std::string m_EnemyJsonPath = "assets/data/enemy_data.json";
    std::string m_SelectedStageId;

    std::filesystem::file_time_type m_LastStageJsonWriteTime{};
    std::filesystem::file_time_type m_LastEnemyJsonWriteTime{};

    int m_HotReloadCheckFrame = 0;

    bool m_HotReloadPending = false;
    int m_HotReloadWaitFrame = 0;

	void UpdateJsonHotReload();
	void ReloadEnemyStatusFromJson();
    void ArrangeDenseEnemySpawns(StageData& stage) const;
    void ValidateEnemySpawns(
        const StageData& stage,
        const PlayerBall& player,
        const TableFrame& tableFrame) const;

    std::filesystem::file_time_type GetJsonWriteTime(const std::string& path) const;
};
