#include "BattleSceneSetup.h"

#include "BattleSceneServices.h"
#include "Collision.h"
#include "PlayerBall.h"
#include "StageDataLoader.h"
#include "TableConfig.h"
#include "TableFrame.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <utility>
#include <vector>

using DirectX::SimpleMath::Vector3;

BattleSceneSetupResult BattleSceneSetup::Prepare() const
{
    BattleSceneSetupResult result{};
    result.stageJsonPath = m_StageJsonPath;
    result.enemyJsonPath = m_EnemyJsonPath;
    result.selectedStageId = m_Services.SelectedStageId();

    const StageData* stageData = m_Services.CurrentStageOverride();
    const bool usesMcpStageOverride = stageData != nullptr;
    std::vector<StageData> stages;
    if (!usesMcpStageOverride)
    {
        stages = StageDataLoader::LoadAll(
            m_StageJsonPath,
            m_EnemyJsonPath);
        stageData = StageDataLoader::FindById(
            stages,
            result.selectedStageId);
    }

    if (stageData == nullptr)
    {
        std::cerr << "[BattleScene] \u4fdd\u5b58\u6e08\u307fstage ID\u300c"
            << result.selectedStageId << "\u300d\u304c\u898b\u3064\u304b\u308a\u307e\u305b\u3093" << std::endl;

        if (!stages.empty())
        {
            stageData = &stages.front();
            result.selectedStageId = stageData->id;
            std::cerr << "[BattleScene] \u5b89\u5168\u306a\u30d5\u30a9\u30fc\u30eb\u30d0\u30c3\u30af\u3068\u3057\u3066\u300c"
                << result.selectedStageId << "\u300d\u3092\u4f7f\u7528\u3057\u307e\u3059" << std::endl;
        }
    }

    if (stageData == nullptr)
    {
        return result;
    }

    StageData adjustedStage = *stageData;
    for (EnemySpawnData& spawn : adjustedStage.enemies)
    {
        m_Services.ApplyEnemyDifficultyScaling(spawn.enemyData);
    }
    if (!usesMcpStageOverride)
    {
        ArrangeDenseEnemySpawns(adjustedStage);
    }
    result.stage = std::move(adjustedStage);
    return result;
}

void BattleSceneSetup::ArrangeDenseEnemySpawns(StageData& stage) const
{
    constexpr std::size_t kDenseColumnCount = 3;
    constexpr float kDenseGap = 2.0f;
    constexpr float kNearestRowDistance = 14.0f;

    if (stage.preserveLayout || stage.enemies.size() <= 3)
    {
        return;
    }

    float maxRadius = 0.01f;
    float originalCenterZ = 0.0f;
    for (const EnemySpawnData& spawn : stage.enemies)
    {
        maxRadius = (std::max)(maxRadius, spawn.enemyData.status.radius);
        originalCenterZ += spawn.position.z;
    }
    originalCenterZ /= static_cast<float>(stage.enemies.size());

    const float spacing = maxRadius * 2.0f + kDenseGap;
    const float zDirection = originalCenterZ < 0.0f ? -1.0f : 1.0f;

    std::size_t enemyIndex = 0;
    std::size_t rowIndex = 0;
    while (enemyIndex < stage.enemies.size())
    {
        const std::size_t enemiesInRow = (std::min)(
            kDenseColumnCount,
            stage.enemies.size() - enemyIndex);
        const float rowStartX =
            -static_cast<float>(enemiesInRow - 1) * spacing * 0.5f;
        const float rowZ = zDirection *
            (kNearestRowDistance + static_cast<float>(rowIndex) * spacing);

        for (std::size_t columnIndex = 0;
            columnIndex < enemiesInRow;
            ++columnIndex, ++enemyIndex)
        {
            EnemySpawnData& spawn = stage.enemies[enemyIndex];
            spawn.position = Vector3(
                rowStartX + static_cast<float>(columnIndex) * spacing,
                TableConfig::FIELD_HEIGHT,
                rowZ);
            spawn.enemyData.initPosition = spawn.position;
        }
        ++rowIndex;
    }
}

void BattleSceneSetup::ValidateEnemySpawns(
    const StageData& stage,
    const PlayerBall& player,
    const TableFrame& tableFrame) const
{
    const float fieldHalfWidth = TableConfig::GetFieldWidth() * 0.5f;
    const float fieldHalfDepth = TableConfig::GetFieldDepth() * 0.5f;
    const std::vector<Collision::Segment> walls = tableFrame.GetWalls();
    const Collision::Sphere playerSphere = player.GetBall()->GetSphere();
    std::vector<Collision::Sphere> validatedEnemies;

    const auto warn = [&stage](
        std::size_t enemyIndex,
        const Vector3& position,
        const char* issue)
    {
        std::cerr << "[StageValidation] Stage ID: " << stage.id
            << " / Enemy Index: " << enemyIndex
            << " / Position: (" << position.x << ", "
            << position.y << ", " << position.z << ")"
            << " / Issue: " << issue << std::endl;
    };

    for (std::size_t enemyIndex = 0;
        enemyIndex < stage.enemies.size();
        ++enemyIndex)
    {
        const EnemySpawnData& spawn = stage.enemies[enemyIndex];
        Vector3 position = spawn.position;
        position.y = TableConfig::FIELD_HEIGHT;
        const float radius =
            (std::max)(0.01f, spawn.enemyData.status.radius);
        const Collision::Sphere enemySphere{ position, radius };

        if (std::abs(position.x) + radius > fieldHalfWidth ||
            std::abs(position.z) + radius > fieldHalfDepth)
        {
            warn(enemyIndex, position, "\u30d7\u30ec\u30a4\u53ef\u80fd\u7bc4\u56f2\u5916");
        }

        for (const Collision::Segment& wall : walls)
        {
            Vector3 wallCheckPosition = position;
            wallCheckPosition.y = wall.start.y;
            if (Collision::DistancePointToSegment(
                wallCheckPosition,
                wall) <= radius)
            {
                warn(enemyIndex, position, "\u58c1\u3068\u91cd\u306a\u3063\u3066\u3044\u307e\u3059");
                break;
            }
        }

        if (Collision::CheckHit(enemySphere, playerSphere))
        {
            warn(enemyIndex, position, "\u30d7\u30ec\u30a4\u30e4\u30fc\u521d\u671f\u4f4d\u7f6e\u3068\u91cd\u306a\u3063\u3066\u3044\u307e\u3059");
        }

        for (const Collision::Sphere& otherEnemy : validatedEnemies)
        {
            if (Collision::CheckHit(enemySphere, otherEnemy))
            {
                warn(enemyIndex, position, "\u5225\u306e\u6575\u3068\u91cd\u306a\u3063\u3066\u3044\u307e\u3059");
                break;
            }
        }

        validatedEnemies.push_back(enemySphere);
    }
}
