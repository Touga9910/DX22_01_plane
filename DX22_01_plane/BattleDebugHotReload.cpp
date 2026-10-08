#include "BattleDebugHotReload.h"

#include "BattleSceneServices.h"
#include "EnemyBall.h"
#include "EnemyData.h"
#include "Input.h"
#include "StageDataLoader.h"

#include <deque>
#include <iostream>
#include <unordered_map>
#include <utility>
#include <vector>

void BattleDebugHotReload::Initialize(
    std::string stageJsonPath,
    std::string enemyJsonPath,
    std::string selectedStageId)
{
    m_StageJsonPath = std::move(stageJsonPath);
    m_EnemyJsonPath = std::move(enemyJsonPath);
    m_SelectedStageId = std::move(selectedStageId);
    m_LastStageJsonWriteTime = GetJsonWriteTime(m_StageJsonPath);
    m_LastEnemyJsonWriteTime = GetJsonWriteTime(m_EnemyJsonPath);
    m_HotReloadCheckFrame = 0;
    m_HotReloadPending = false;
    m_HotReloadWaitFrame = 0;
}

void BattleDebugHotReload::Update()
{
    if (m_Services.IsDebugMode())
    {
        return;
    }

    UpdateJsonHotReload();
    if (Input::GetKeyTrigger(VK_F5))
    {
        ReloadEnemyStatusFromJson();
    }
}

void BattleDebugHotReload::UpdateJsonHotReload()
{
    if (m_Services.CurrentStageOverride() != nullptr)
    {
        return;
    }

    if (m_HotReloadPending)
    {
        ++m_HotReloadWaitFrame;
        if (m_HotReloadWaitFrame < 30)
        {
            return;
        }

        m_HotReloadPending = false;
        m_HotReloadWaitFrame = 0;
        std::cout << "[HotReload] JSON\u3092\u518d\u8aad\u307f\u8fbc\u307f\u3057\u307e\u3059" << std::endl;
        ReloadEnemyStatusFromJson();
        return;
    }

    ++m_HotReloadCheckFrame;
    if (m_HotReloadCheckFrame < 60)
    {
        return;
    }
    m_HotReloadCheckFrame = 0;

    const auto currentStageWriteTime =
        GetJsonWriteTime(m_StageJsonPath);
    const auto currentEnemyWriteTime =
        GetJsonWriteTime(m_EnemyJsonPath);
    const bool stageChanged =
        currentStageWriteTime != m_LastStageJsonWriteTime;
    const bool enemyChanged =
        currentEnemyWriteTime != m_LastEnemyJsonWriteTime;
    if (!stageChanged && !enemyChanged)
    {
        return;
    }

    m_LastStageJsonWriteTime = currentStageWriteTime;
    m_LastEnemyJsonWriteTime = currentEnemyWriteTime;
    m_HotReloadPending = true;
    m_HotReloadWaitFrame = 0;
    std::cout << "[HotReload] JSON\u5909\u66f4\u3092\u691c\u77e5\u3057\u307e\u3057\u305f\u3002\u5c11\u3057\u5f85\u3063\u3066\u304b\u3089\u518d\u8aad\u307f\u8fbc\u307f\u3057\u307e\u3059\u3002"
        << std::endl;
}

void BattleDebugHotReload::ReloadEnemyStatusFromJson()
{
    const std::vector<StageData> stages = StageDataLoader::LoadAll(
        m_StageJsonPath,
        m_EnemyJsonPath);
    const StageData* stageData =
        StageDataLoader::FindById(stages, m_SelectedStageId);
    if (stageData == nullptr)
    {
        std::cerr << "[HotReload] \u73fe\u5728\u306estage ID\u300c" << m_SelectedStageId
            << "\u300d\u304c\u518d\u8aad\u8fbc\u5f8c\u306eJSON\u306b\u5b58\u5728\u3057\u306a\u3044\u305f\u3081\u3001\u53cd\u6620\u3092\u4e2d\u6b62\u3057\u307e\u3057\u305f"
            << std::endl;
        return;
    }
    if (stageData->enemies.empty())
    {
        std::cout << "[HotReload] \u6575\u30c7\u30fc\u30bf\u304c\u7a7a\u306e\u305f\u3081\u3001\u53cd\u6620\u3092\u4e2d\u6b62\u3057\u307e\u3057\u305f"
            << std::endl;
        return;
    }

    std::unordered_map<std::string, std::deque<EnemyData>> enemyDataMap;
    for (const EnemySpawnData& spawn : stageData->enemies)
    {
        EnemyData enemyData = spawn.enemyData;
        m_Services.ApplyEnemyDifficultyScaling(enemyData);
        enemyDataMap[enemyData.id].push_back(std::move(enemyData));
    }

    const std::vector<EnemyBall*> enemies = m_Services.Enemies();
    for (EnemyBall* enemy : enemies)
    {
        if (enemy == nullptr)
        {
            continue;
        }
        auto found = enemyDataMap.find(enemy->GetEnemyId());
        if (found == enemyDataMap.end() || found->second.empty())
        {
            continue;
        }
        enemy->ApplyHotReloadData(found->second.front());
        found->second.pop_front();
    }

    m_Services.InvalidateCombatForecast(
        "\u30a8\u30cd\u30df\u30fc\u30b9\u30c6\u30fc\u30bf\u30b9\u518d\u8aad\u8fbc");
    std::cout << "[HotReload] \u6575\u30b9\u30c6\u30fc\u30bf\u30b9\u3092\u66f4\u65b0\u3057\u307e\u3057\u305f" << std::endl;
}

std::filesystem::file_time_type BattleDebugHotReload::GetJsonWriteTime(
    const std::string& path) const
{
    try
    {
        if (std::filesystem::exists(path))
        {
            return std::filesystem::last_write_time(path);
        }
    }
    catch (const std::exception& error)
    {
        std::cout << "[HotReload] JSON\u66f4\u65b0\u6642\u523b\u306e\u53d6\u5f97\u306b\u5931\u6557: "
            << path << std::endl;
        std::cout << error.what() << std::endl;
    }
    return {};
}
