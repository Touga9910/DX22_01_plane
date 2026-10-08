#include "BallFactory.h"
#include "BreakBall.h"

#include "BallComponent.h"
#include "BallCollisionComponent.h"
#include "BallPhysicsComponent.h"
#include "BallRenderComponent.h"
#include "BallStatusComponent.h"
#include "EnemyBall.h"
#include "EnemyAttackComponent.h"
#include "GameRuntime.h"
#include "GameObject.h"
#include "PlayerBall.h"
#include "NuisanceBall.h"
#include "SphereColliderComponent.h"
#include "TagComponent.h"

namespace
{
    GameObject* ConfigureBallObject(
        GameObject* object,
        GameObjectTag tag)
    {
        object->AddComponent<TagComponent>(tag);
        object->AddComponent<BallStatusComponent>();
        BallPhysicsComponent* physics =
            object->AddComponent<BallPhysicsComponent>();
        object->AddComponent<SphereColliderComponent>(
            physics->Radius(),
            false);
        object->AddComponent<BallRenderComponent>();
        object->AddComponent<BallCollisionComponent>();
        object->AddComponent<BallComponent>();

        return object;
    }

    GameObject* CreateBallObject(
        Game& game,
        const char* name,
        GameObjectTag tag)
    {
        return ConfigureBallObject(
            GameRuntime::CreateObject(game, name), tag);
    }

    GameObject* CreateBallObject(
        const char* name,
        GameObjectTag tag)
    {
        return ConfigureBallObject(
            GameRuntime::CreateObject(name), tag);
    }
}

BreakBall* BallFactory::CreateBreakBall(Game& game, int index)
{
    auto* object = CreateBallObject(game, "BreakBall", GameObjectTag::None);
    return object->AddComponent<BreakBall>(index);
}

PlayerBall* BallFactory::CreatePlayer(Game& game)
{
    GameObject* object = CreateBallObject(
        game,
        "PlayerBall",
        GameObjectTag::Player);

    return object->AddComponent<PlayerBall>();
}

EnemyBall* BallFactory::CreateEnemy(Game& game, const EnemyData& data)
{
    GameObject* object = CreateBallObject(
        game,
        "EnemyBall",
        GameObjectTag::Enemy);

    EnemyBall* enemy = object->AddComponent<EnemyBall>(data);
    object->AddComponent<EnemyAttackComponent>();
    return enemy;
}

NuisanceBall* BallFactory::CreateNuisanceBall(
    const NuisanceBallData& data,
    const DirectX::SimpleMath::Vector3& position)
{
    GameObject* object = CreateBallObject(
        "NuisanceBall",
        GameObjectTag::None);
    return object->AddComponent<NuisanceBall>(data, position);
}
