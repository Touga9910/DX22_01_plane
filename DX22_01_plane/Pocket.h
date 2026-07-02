#pragma once

#include "Object.h"
#include "Collision.h"

class Pocket : public Object
{
private:
    float m_Radius = 2.0f;

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void Uninit() override;

    void SetPosition(const DirectX::SimpleMath::Vector3& position);
    void SetRadius(float radius);

    Collision::Sphere GetSphere() const;
};