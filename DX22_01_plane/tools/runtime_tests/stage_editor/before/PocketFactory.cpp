#include "PocketFactory.h"

#include "Game.h"
#include "GameObject.h"
#include "Pocket.h"
#include "SphereColliderComponent.h"
#include "TagComponent.h"

Pocket* PocketFactory::Create(
    Game& game,
    const DirectX::SimpleMath::Vector3& position,
    float radius)
{
    GameObject* object = game.CreateGameObject("Pocket");

    object->AddComponent<TagComponent>(GameObjectTag::Pocket);
    object->AddComponent<SphereColliderComponent>(radius, true);
    Pocket* pocket = object->AddComponent<Pocket>(radius);
    pocket->SetPosition(position);

    return pocket;
}
