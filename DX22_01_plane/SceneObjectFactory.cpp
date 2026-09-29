#include "SceneObjectFactory.h"

#include "GameRuntime.h"
#include "Texture2DFactory.h"

Texture2D* SceneObjectFactory::CreateTexture2D()
{
    return Texture2DFactory::Create(GameRuntime::CurrentGame());
}
