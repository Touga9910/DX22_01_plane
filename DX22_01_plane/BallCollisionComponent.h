#pragma once

#include "Collision.h"
#include "Component.h"
#include "SimpleMath.h"

#include <vector>

class BallComponent;
class BallPhysicsComponent;
class Pocket;

class BallCollisionComponent final : public Component
{
public:
    void Awake() override;

    void ResolveMovementAndCollisions(BallComponent& ball);
    void ResetShotAbilityState();

private:
    DirectX::SimpleMath::Vector3 GetPosition() const;
    void SetPosition(const DirectX::SimpleMath::Vector3& position);
    void Translate(const DirectX::SimpleMath::Vector3& movement);
    Collision::Sphere GetSphere() const;
    int GetAttack() const;
    bool HasPierceAbility() const;
    bool CheckPocketHitAlongMovement(
        const DirectX::SimpleMath::Vector3& movementStart,
        const std::vector<Pocket*>& pockets);
    void OnPocketHit();

private:
    BallComponent* m_BallComponent = nullptr;
    BallPhysicsComponent* m_PhysicsComponent = nullptr;
    int m_PierceUseCount = 0;
    const BallComponent* m_PiercedBall = nullptr;
};
