#pragma once

#include "BallPhysicsComponent.h"
#include "BallRenderComponent.h"
#include "BallStatusComponent.h"
#include "Collision.h"
#include "Component.h"
#include "SphereColliderComponent.h"
#include "transform.h"

#include <functional>
#include <string>

// Common ball mechanics attached beside player/enemy controller components.
class BallComponent final : public Component
{
public:
    void Awake() override;
    void LateUpdate() override;

    void Damage(int damage);
    void TakeDamage(int damage);
    void Defeat();

    bool IsDefeated() const { return m_StatusComponent->IsDefeated(); }
    bool IsHpZero() const { return GetHP() <= 0; }
    void ResetDefeated();

    void SetStatus(const BallStatus& status);
    const BallStatus& GetStatus() const { return m_StatusComponent->GetStatus(); }

    int GetHP() const { return m_StatusComponent->GetCurrentHp(); }
    int GetMaxHP() const { return m_StatusComponent->GetMaxHp(); }
    int GetAttack() const { return m_StatusComponent->GetAttack(); }
    int GetDefense() const { return m_StatusComponent->GetDefense(); }
    bool HasSplitAbility() const { return GetStatus().abilities.split; }
    bool HasPierceAbility() const { return GetStatus().abilities.pierce; }

    void SetHP(int hp);
    void SetMaxHP(int maxHp);
    void ApplyStatusValuesOnly(const BallStatus& status);

    void UpdatePhysics();
    void ResetToInitialPosition();
    void OnPocketHit();

    void SetInitialPosition(const DirectX::SimpleMath::Vector3& position)
    {
        m_PhysicsComponent->InitialPosition() = position;
    }

    DirectX::SimpleMath::Vector3 GetVelocity() const { return m_PhysicsComponent->GetVelocity(); }
    DirectX::SimpleMath::Quaternion GetRollingRotation() const { return m_PhysicsComponent->GetRollingRotation(); }
    float GetRadius() const { return m_PhysicsComponent->GetRadius(); }
    DirectX::SimpleMath::Vector3 GetPosition() const { return m_Transform.position; }

    void SetRadius(float radius);
    Collision::Sphere GetSphere() const { return { m_Transform.position, GetRadius() }; }
    bool IsStopped() const { return m_PhysicsComponent->IsStopped(); }

    void LoadModel(const char* modelFilePath, const char* textureDirectory);
    void BeginDraw();
    void DrawMesh(const DirectX::SimpleMath::Matrix& worldMatrix);
    void DrawImGui(const std::string& label);

    void UpdateRadius();
    void SynchronizeComponents();

    void SetPocketHandler(std::function<void()> handler)
    {
        m_PocketHandler = std::move(handler);
    }

    void SetDefeatHandler(std::function<void()> handler)
    {
        m_DefeatHandler = std::move(handler);
    }

    void Destroy();

    Transform& GetMutableTransform() { return m_Transform; }
    DirectX::SimpleMath::Vector3& GetMutableVelocity() { return m_PhysicsComponent->Velocity(); }
    DirectX::SimpleMath::Vector3& GetMutableAcceleration() { return m_PhysicsComponent->Acceleration(); }
    DirectX::SimpleMath::Quaternion& GetMutableRollingRotation() { return m_PhysicsComponent->RollingRotation(); }
    float& GetMutableFriction() { return m_PhysicsComponent->Friction(); }

private:
    void ApplyStatusValues(const BallStatus& status);

private:
    BallStatusComponent* m_StatusComponent = nullptr;
    BallPhysicsComponent* m_PhysicsComponent = nullptr;
    SphereColliderComponent* m_ColliderComponent = nullptr;
    BallRenderComponent* m_RenderComponent = nullptr;

    Transform m_Transform;
    std::function<void()> m_PocketHandler;
    std::function<void()> m_DefeatHandler;
};
