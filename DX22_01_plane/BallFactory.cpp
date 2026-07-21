#include "BallFactory.h"

#include "BallComponent.h"
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
        object->AddComponent<BallPhysicsComponent>();
        object->AddComponent<SphereColliderComponent>(2.0f, false);
        object->AddComponent<BallRenderComponent>();
        object->AddComponent<BallComponent>();

        return object;
    }
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
