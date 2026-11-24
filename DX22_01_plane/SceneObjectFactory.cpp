#include "SceneObjectFactory.h"

#include "GameRuntime.h"
#include "Texture2DFactory.h"

/// <summary>
/// 現在のゲームに2Dテクスチャ表示用のオブジェクトを生成する。
/// </summary>
Texture2D* SceneObjectFactory::CreateTexture2D()
{
    return Texture2DFactory::Create(GameRuntime::CurrentGame());
}
