#include "BattleSceneObjectFactory.h"

#include "BallFactory.h"
#include "BattleSceneServices.h"
#include "BreakBall.h"
#include "EnemyBall.h"
#include "GameRuntime.h"
#include "GameObject.h"
#include "Ground.h"
#include "GroundRenderComponent.h"
#include "PlayerBall.h"
#include "Pocket.h"
#include "PocketFactory.h"
#include "StageData.h"
#include "TableConfig.h"
#include "TableFrame.h"
#include "TableFrameCollisionComponent.h"
#include "TableFrameRenderComponent.h"
#include "TagComponent.h"

#include <algorithm>
#include <typeinfo>

BattleArenaObjects BattleSceneObjectFactory::CreateArena(
    std::vector<GameObject*>& sceneObjects) const
{
    Game& game = m_Services.GameContext();

    PlayerBall* player = BallFactory::CreatePlayer(game);
    m_Services.ApplyDebugPlayer(player);
    sceneObjects.emplace_back(player->GetGameObject());

    GameObject* groundObject =
        GameRuntime::CreateObject(game, typeid(Ground).name());
    groundObject->AddComponent<TagComponent>(GameObjectTag::Ground);
    groundObject->AddComponent<GroundRenderComponent>();
    groundObject->AddComponent<Ground>();
    sceneObjects.emplace_back(groundObject);

    GameObject* tableFrameObject =
        GameRuntime::CreateObject(game, typeid(TableFrame).name());
    tableFrameObject->AddComponent<TagComponent>(GameObjectTag::Rail);
    tableFrameObject->AddComponent<TableFrameCollisionComponent>();
    tableFrameObject->AddComponent<TableFrameRenderComponent>();
    TableFrame* tableFrame = tableFrameObject->AddComponent<TableFrame>();
    sceneObjects.emplace_back(tableFrameObject);

    for (const Collision::Sphere& pocketSphere : tableFrame->GetPocketSpheres())
    {
        Pocket* pocket = PocketFactory::Create(
            game,
            pocketSphere.center,
            pocketSphere.radius);
        sceneObjects.emplace_back(pocket->GetGameObject());
    }

    return { player, tableFrame };
}

void BattleSceneObjectFactory::CreateStageObjects(
    const StageData& stage,
    std::vector<GameObject*>& sceneObjects) const
{
    Game& game = m_Services.GameContext();

    for (std::size_t enemyIndex = 0;
        enemyIndex < stage.enemies.size();
        ++enemyIndex)
    {
        const EnemySpawnData& spawn = stage.enemies[enemyIndex];
        EnemyData enemyData = spawn.enemyData;
        enemyData.initPosition = spawn.position;
        enemyData.initPosition.y = TableConfig::FIELD_HEIGHT;

        EnemyBall* enemy = BallFactory::CreateEnemy(game, enemyData);
        m_Services.ApplyDebugEnemy(enemy, enemyIndex);
        sceneObjects.emplace_back(enemy->GetGameObject());
    }

    m_Services.NotifyStageStarted(stage);
    const bool hasArmorBoss = std::any_of(
        stage.enemies.begin(),
        stage.enemies.end(),
        [](const EnemySpawnData& spawn)
        {
            return spawn.enemyData.id == "enemy_boss_core";
        });
    if (!hasArmorBoss)
    {
        return;
    }

    const bool usePlacedPositions =
        m_Services.IsDebugMode() || stage.hasBreakBallLayout;
    const auto placedPositions = m_Services.IsDebugMode()
        ? m_Services.DebugBreakBallPositions()
        : stage.breakBallPositions;
    const int count = usePlacedPositions
        ? static_cast<int>(placedPositions.size())
        : 2;
    for (int index = 0; index < count; ++index)
    {
        BreakBall* neutral = BallFactory::CreateBreakBall(game, index);
        sceneObjects.emplace_back(neutral->GetGameObject());
        if (usePlacedPositions)
        {
            neutral->PlaceAt(placedPositions[static_cast<std::size_t>(index)]);
        }
        else
        {
            neutral->Reposition();
        }
    }
}

void BattleSceneObjectFactory::BeginSimulation(PlayerBall& player) const
{
    player.SetState(PlayerBall::State::Simulation);
}
