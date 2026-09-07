#include "BallFactory.h"
#include "BreakBall.h"

#include "BallComponent.h"
#include "BallCollisionComponent.h"
#include "BallPhysicsComponent.h"
#include "BallRenderComponent.h"
#include "BallStatusComponent.h"
#include "EnemyBall.h"
#include "EnemyAttackComponent.h"
#include "Game.h"
#include "GameObject.h"
#include "PlayerBall.h"
#include "SphereColliderComponent.h"
#include "TagComponent.h"

namespace
{
    GameObject* CreateBallObject(
        Game& game,
        const char* name,
        GameObjectTag tag)
    {
        GameObject* object = game.CreateGameObject(name);

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
