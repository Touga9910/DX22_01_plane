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

    /// <summary>
    /// ステータスコンポーネントが保持する撃破状態を返す。
    /// </summary>
    bool IsDefeated() const { return m_StatusComponent->IsDefeated(); }
    /// <summary>
    /// ボールの現在HPがゼロ以下かを判定する。
    /// </summary>
    bool IsHpZero() const { return GetHP() <= 0; }
    void ResetDefeated();

    void SetStatus(const BallStatus& status);
    /// <summary>
    /// ボールのステータスを読み取り専用の参照で返す。
    /// </summary>
    const BallStatus& GetStatus() const { return m_StatusComponent->GetStatus(); }

    /// <summary>
    /// ボールの現在HPを返す。
    /// </summary>
    int GetHP() const { return m_StatusComponent->GetCurrentHp(); }
    /// <summary>
    /// ボールの最大HPを返す。
    /// </summary>
    int GetMaxHP() const { return m_StatusComponent->GetMaxHp(); }
    /// <summary>
    /// 基礎攻撃力に状態効果とオーラの補正を加え、非負の攻撃力を返す。
    /// </summary>
    int GetAttack() const
    {
        return (std::max)(0, m_StatusComponent->GetAttack() +
            m_StatusEffects.GetAttackModifier() +
            m_AuraStatusEffects.GetAttackModifier());
    }
    /// <summary>
    /// 基礎防御力に状態効果とオーラの補正を加え、非負の防御力を返す。
    /// </summary>
    int GetDefense() const
    {
        return (std::max)(0, m_StatusComponent->GetDefense() +
            m_StatusEffects.GetDefenseModifier() +
            m_AuraStatusEffects.GetDefenseModifier());
    }
    /// <summary>
    /// 入力値から防御力を差し引き、被ダメージを最低1に揃える。
    /// </summary>
    int CalculateDamageTaken(int damage) const
    {
        return (std::max)(1, damage - GetDefense());
    }
    /// <summary>
    /// ステータスコンポーネントに攻撃力と防御力の戦闘補正を設定する。
    /// </summary>
    void SetCombatModifiers(int attackModifier, int defenseModifier)
    {
        m_StatusComponent->SetCombatModifiers(
            attackModifier,
            defenseModifier);
    }
    /// <summary>
    /// ボールに直接かかる状態効果を設定する。
    /// </summary>
    void SetStatusEffects(const StatusEffectCollection& effects)
    {
        m_StatusEffects = effects;
    }
    /// <summary>
    /// ボールの状態効果を変更できる参照を返す。
    /// </summary>
    StatusEffectCollection& GetMutableStatusEffects() { return m_StatusEffects; }
    /// <summary>
    /// ボールの状態効果を読み取り専用の参照で返す。
    /// </summary>
    const StatusEffectCollection& GetStatusEffects() const { return m_StatusEffects; }
    /// <summary>
    /// オーラによる状態効果を設定する。
    /// </summary>
    void SetAuraStatusEffects(const StatusEffectCollection& effects)
    {
        m_AuraStatusEffects = effects;
    }
    /// <summary>
    /// オーラによる状態効果を読み取り専用の参照で返す。
    /// </summary>
    const StatusEffectCollection& GetAuraStatusEffects() const
    {
        return m_AuraStatusEffects;
    }
    /// <summary>
    /// ボールのステータスから分裂能力の有無を返す。
    /// </summary>
    bool HasSplitAbility() const { return GetStatus().abilities.split; }
    /// <summary>
    /// ボールのステータスから貫通能力の有無を返す。
    /// </summary>
    bool HasPierceAbility() const { return GetStatus().abilities.pierce; }
    /// <summary>
    /// ボールのステータスからアンカー能力の有無を返す。
    /// </summary>
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

    /// <summary>
    /// リセット時に使う初期位置を物理コンポーネントに保存する。
    /// </summary>
    void SetInitialPosition(const DirectX::SimpleMath::Vector3& position)
    {
        m_PhysicsComponent->InitialPosition() = position;
    }

    /// <summary>
    /// 物理コンポーネントからボールの現在速度を返す。
    /// </summary>
    DirectX::SimpleMath::Vector3 GetVelocity() const { return m_PhysicsComponent->GetVelocity(); }
    /// <summary>
    /// 物理コンポーネントから転がり回転を返す。
    /// </summary>
    DirectX::SimpleMath::Quaternion GetRollingRotation() const { return m_PhysicsComponent->GetRollingRotation(); }
    /// <summary>
    /// 物理コンポーネントからボールの半径を返す。
    /// </summary>
    float GetRadius() const { return m_PhysicsComponent->GetRadius(); }
    DirectX::SimpleMath::Vector3 GetPosition() const;
    DirectX::SimpleMath::Quaternion GetRotation() const;
    DirectX::SimpleMath::Vector3 GetScale() const;
    void SetPosition(const DirectX::SimpleMath::Vector3& position);
    void Translate(const DirectX::SimpleMath::Vector3& movement);
    void SetRotation(const DirectX::SimpleMath::Quaternion& rotation);
    void SetScale(const DirectX::SimpleMath::Vector3& scale);

    void SetRadius(float radius);
    /// <summary>
    /// ボールの中心位置と半径を衝突判定用の球として返す。
    /// </summary>
    Collision::Sphere GetSphere() const { return { GetPosition(), GetRadius() }; }
    /// <summary>
    /// 物理コンポーネントの速度判定からボールの停止状態を返す。
    /// </summary>
    bool IsStopped() const { return m_PhysicsComponent->IsStopped(); }

    void DrawImGui(const std::string& label);

    void UpdateRadius();
    void SynchronizeComponents();

    /// <summary>
    /// ポケット接触時に実行する関数を登録する。
    /// </summary>
    void SetPocketHandler(std::function<void()> handler)
    {
        m_PocketHandler = std::move(handler);
    }

    /// <summary>
    /// ボールが撃破されたときに実行する関数を登録する。
    /// </summary>
    void SetDefeatHandler(std::function<void()> handler)
    {
        m_DefeatHandler = std::move(handler);
    }

    void Destroy();

    /// <summary>
    /// 物理コンポーネントの速度を変更できる参照を返す。
    /// </summary>
    DirectX::SimpleMath::Vector3& GetMutableVelocity() { return m_PhysicsComponent->Velocity(); }
    /// <summary>
    /// 物理コンポーネントの加速度を変更できる参照を返す。
    /// </summary>
    DirectX::SimpleMath::Vector3& GetMutableAcceleration() { return m_PhysicsComponent->Acceleration(); }
    /// <summary>
    /// 物理コンポーネントの転がり回転を変更できる参照を返す。
    /// </summary>
    DirectX::SimpleMath::Quaternion& GetMutableRollingRotation() { return m_PhysicsComponent->RollingRotation(); }
    /// <summary>
    /// 物理コンポーネントの摩擦係数を変更できる参照を返す。
    /// </summary>
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
    // Rebuilt from currently active field objects. It never overwrites the
    // player's permanent/run status effects.
    StatusEffectCollection m_AuraStatusEffects;

    std::function<void()> m_PocketHandler;
    std::function<void()> m_DefeatHandler;

};
