#pragma once

#include "BallPhysicsComponent.h"
#include "BallRenderComponent.h"
#include "BallStatusComponent.h"
#include "Collision.h"
#include "Component.h"
#include "SphereColliderComponent.h"
#include "StatusEffect.h"

#include <algorithm>
#include <functional>
#include <string>

class BallCollisionComponent;

// プレイヤーおよび敵の制御コンポーネントと併用する、ボール共通処理。
class BallComponent final : public Component
{
public:
    void Awake() override;

    void Damage(int damage);
	void DamageAfterDefense(int damage);
    void TakeDamage(int damage);
    void Defeat();

    bool IsDefeated() const { return m_StatusComponent->IsDefeated(); }
    bool IsHpZero() const { return GetHP() <= 0; }
    void ResetDefeated();

    void SetStatus(const BallStatus& status);
    const BallStatus& GetStatus() const { return m_StatusComponent->GetStatus(); }

    int GetHP() const { return m_StatusComponent->GetCurrentHp(); }
    int GetMaxHP() const { return m_StatusComponent->GetMaxHp(); }
    int GetAttack() const
    {
        return (std::max)(0, m_StatusComponent->GetAttack() +
            m_StatusEffects.GetAttackModifier());
    }
    int GetDefense() const
    {
        return (std::max)(0, m_StatusComponent->GetDefense() +
            m_StatusEffects.GetDefenseModifier());
    }
    int CalculateDamageTaken(int damage) const
    {
        return (std::max)(1, damage - GetDefense());
    }
    void SetCombatModifiers(int attackModifier, int defenseModifier)
    {
        m_StatusComponent->SetCombatModifiers(
            attackModifier,
            defenseModifier);
    }
    void SetStatusEffects(const StatusEffectCollection& effects)
    {
        m_StatusEffects = effects;
    }
    StatusEffectCollection& GetMutableStatusEffects() { return m_StatusEffects; }
    const StatusEffectCollection& GetStatusEffects() const { return m_StatusEffects; }
    bool HasSplitAbility() const { return GetStatus().abilities.split; }
    bool HasPierceAbility() const { return GetStatus().abilities.pierce; }
    bool HasAnchorAbility() const { return GetStatus().abilities.anchor; }
    void ResetShotAbilityState();

    void SetHP(int hp);
    void SetMaxHP(int maxHp);
    void ApplyStatusValuesOnly(const BallStatus& status);

    void BeginPhysicsStep();
    void FinishPhysicsStep();
    void ResetToInitialPosition();
    void ResetAtPosition(
        const DirectX::SimpleMath::Vector3& position);
    void OnPocketHit();

    void SetInitialPosition(const DirectX::SimpleMath::Vector3& position)
    {
        m_PhysicsComponent->InitialPosition() = position;
    }

    DirectX::SimpleMath::Vector3 GetVelocity() const { return m_PhysicsComponent->GetVelocity(); }
    DirectX::SimpleMath::Quaternion GetRollingRotation() const { return m_PhysicsComponent->GetRollingRotation(); }
    float GetRadius() const { return m_PhysicsComponent->GetRadius(); }
    DirectX::SimpleMath::Vector3 GetPosition() const;
    DirectX::SimpleMath::Quaternion GetRotation() const;
    DirectX::SimpleMath::Vector3 GetScale() const;
    void SetPosition(const DirectX::SimpleMath::Vector3& position);
    void Translate(const DirectX::SimpleMath::Vector3& movement);
    void SetRotation(const DirectX::SimpleMath::Quaternion& rotation);
    void SetScale(const DirectX::SimpleMath::Vector3& scale);

    void SetRadius(float radius);
    Collision::Sphere GetSphere() const { return { GetPosition(), GetRadius() }; }
    bool IsStopped() const { return m_PhysicsComponent->IsStopped(); }

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
    BallCollisionComponent* m_BallCollisionComponent = nullptr;
    StatusEffectCollection m_StatusEffects;

    std::function<void()> m_PocketHandler;
    std::function<void()> m_DefeatHandler;

};
