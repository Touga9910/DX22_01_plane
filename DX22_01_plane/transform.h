#pragma once
#include <simplemath.h>

class Transform
{
public:
    // SRT情報：外部からアクセスしやすいようにpublicにしておく
    DirectX::SimpleMath::Vector3 position = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
    DirectX::SimpleMath::Vector3 rotation = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
    DirectX::SimpleMath::Vector3 scale = DirectX::SimpleMath::Vector3(1.0f, 1.0f, 1.0f);

    // デフォルトコンストラクタ
    Transform() {}
    ~Transform() {}

    // 移動の挙動
    void Translate(const DirectX::SimpleMath::Vector3& translation)
    {
        position += translation;
    }

    // 回転の挙動
    void Rotate(const DirectX::SimpleMath::Vector3& rot)
    {
        rotation += rot;
    }

    // ワールド行列の計算
    DirectX::SimpleMath::Matrix GetWorldMatrix() const
    {
        return DirectX::SimpleMath::Matrix::CreateScale(scale) *
            DirectX::SimpleMath::Matrix::CreateFromYawPitchRoll(rotation.y, rotation.x, rotation.z) *
            DirectX::SimpleMath::Matrix::CreateTranslation(position);
    }
};