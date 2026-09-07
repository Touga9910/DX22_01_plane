#include "Texture2DFactory.h"

#include "Game.h"
#include "GameObject.h"
#include "TagComponent.h"
#include "Texture2D.h"

#include <typeinfo>

Texture2D* Texture2DFactory::Create(Game& game)
{
    GameObject* object =
        game.CreateGameObject(typeid(Texture2D).name());

    object->AddComponent<TagComponent>(GameObjectTag::ScreenUi);
    return object->AddComponent<Texture2D>();
}
