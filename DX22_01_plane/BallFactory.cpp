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
#include "PierceMinion.h"
#include "SphereColliderComponent.h"
#include "TagComponent.h"

namespace
{
    /// <summary>
    /// 新しいボールオブジェクトにタグ・ステータス・物理・描画などの共通コンポーネントを取り付ける。
    /// </summary>
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

    /// <summary>
    /// 指定したゲームにボールオブジェクトを作成し、共通コンポーネントを設定する。
    /// </summary>
    GameObject* CreateBallObject(
        Game& game,
        const char* name,
        GameObjectTag tag)
    {
        return ConfigureBallObject(
            GameRuntime::CreateObject(game, name), tag);
    }

    /// <summary>
    /// 現在のゲームにボールオブジェクトを作成し、共通コンポーネントを設定する。
    /// </summary>
    GameObject* CreateBallObject(
        const char* name,
        GameObjectTag tag)
    {
        return ConfigureBallObject(
            GameRuntime::CreateObject(name), tag);
    }
}

/// <summary>
/// 指定番号を持つブレイクボールを作成する。
/// </summary>
BreakBall* BallFactory::CreateBreakBall(Game& game, int index)
{
    auto* object = CreateBallObject(game, "BreakBall", GameObjectTag::None);
    return object->AddComponent<BreakBall>(index);
}

/// <summary>
/// プレイヤータグ付きのオブジェクトを作り、プレイヤーボールを取り付ける。
/// </summary>
PlayerBall* BallFactory::CreatePlayer(Game& game)
{
    GameObject* object = CreateBallObject(
        game,
        "PlayerBall",
        GameObjectTag::Player);

    return object->AddComponent<PlayerBall>();
}

/// <summary>
/// 敵タグ付きのボールを指定データで作成し、敵の攻撃コンポーネントを設定する。
/// </summary>
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

/// <summary>
/// 指定データと位置に基づいてお邪魔ボールを作成する。
/// </summary>
NuisanceBall* BallFactory::CreateNuisanceBall(
    const NuisanceBallData& data,
    const DirectX::SimpleMath::Vector3& position)
{
    GameObject* object = CreateBallObject(
        "NuisanceBall",
        GameObjectTag::None);
    return object->AddComponent<NuisanceBall>(data, position);
}

/// <summary>
/// 指定ステータス、配置、発射方向と速度で貫通ミニオンを作成する。
/// </summary>
PierceMinion* BallFactory::CreatePierceMinion(
	Game& game,
	const BallStatus& status,
	const DirectX::SimpleMath::Vector3& position,
	const DirectX::SimpleMath::Vector3& direction,
	float launchSpeed)
{
	GameObject* object = CreateBallObject(
		game,
		"PierceMinion",
		GameObjectTag::None);
	return object->AddComponent<PierceMinion>(
		status,
		position,
		direction,
		launchSpeed);
}
