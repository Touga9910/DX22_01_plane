#pragma once

#include "SimpleMath.h"

class Game;
class Pocket;

// ポケット用GameObjectと必要なコンポーネントをまとめて生成するFactory
class PocketFactory final
{
public:
    // Pocketタグ、Trigger用SphereColliderComponent、Pocketを持つGameObjectを生成
    // positionとradiusを設定したPocketを返す
    static Pocket* Create(
        Game& game,
        const DirectX::SimpleMath::Vector3& position,
        float radius = 2.0f);
};
