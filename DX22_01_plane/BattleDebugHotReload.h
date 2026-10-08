#pragma once

#include <filesystem>
#include <string>

class BattleSceneServices;

class BattleDebugHotReload
{
public:
    explicit BattleDebugHotReload(BattleSceneServices& services)
        : m_Services(services)
    {
    }

    void Initialize(
        std::string stageJsonPath,
        std::string enemyJsonPath,
        std::string selectedStageId);
    void Update();

private:
    void UpdateJsonHotReload();
    void ReloadEnemyStatusFromJson();
    std::filesystem::file_time_type GetJsonWriteTime(
        const std::string& path) const;

    BattleSceneServices& m_Services;
    std::string m_StageJsonPath;
    std::string m_EnemyJsonPath;
    std::string m_SelectedStageId;
    std::filesystem::file_time_type m_LastStageJsonWriteTime{};
    std::filesystem::file_time_type m_LastEnemyJsonWriteTime{};
    int m_HotReloadCheckFrame = 0;
    bool m_HotReloadPending = false;
    int m_HotReloadWaitFrame = 0;
};
