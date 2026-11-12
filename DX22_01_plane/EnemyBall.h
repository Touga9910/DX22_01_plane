#pragma once

#include "BallComponent.h"
#include "BattleTypes.h"
#include "EnemyData.h"
#include "BossCombatRules.h"

#include <string>
#include <optional>
#include <cstdint>
#include <unordered_map>

class PlayerBall;
class Camera;
class BallRenderComponent;
class NuisanceBall;

class EnemyBall final : public Component
{
public:
    EnemyBall() = default;
    explicit EnemyBall(const EnemyData& data) : m_InitialData(data) {}
    ~EnemyBall() = default;

    // -------------------------
    // 基本処理
    // -------------------------
    void Awake() override;
    void Init();
    void Init(const EnemyData& data);
    void FixedUpdate() override;
    void Draw() override;
    void OnDestroy() override;

    // -------------------------
    // 戦闘・状態処理
    // -------------------------
    void Defeat();
    void RemoveFromFieldAfterPocket();
    // Defeated and pocketed balls remain physical objects, but no longer
    // participate in any combat-facing system.
    bool CanParticipateInCombat() const
    {
        return m_Ball != nullptr && !IsDefeated() && !IsPocketed();
    }
    void TakeDamage(
        int damage,
        EnemyDamageKind damageKind = EnemyDamageKind::Other);
	int CalculateDamageTaken(int damage) const;

    // The planner assigns one immutable action before the shot.  EnemyBall
    // keeps only its own assignment and Charge/interruption state.
    void SetPlannedAction(
        EnemyActionType action,
        int budgetCost,
        float chargeDamageMultiplier);
    EnemyActionType GetPlannedAction() const { return m_PlannedAction; }
    int GetPlannedActionCost() const { return m_PlannedActionCost; }
    float GetPlannedDamageMultiplier() const
    {
        return m_PlannedAction == EnemyActionType::ChargeAttack
            ? m_PlannedChargeDamageMultiplier
            : 1.0f;
    }
    bool CanUseCharge() const
    {
        return CanParticipateInCombat() && m_EnemyData.canCharge;
    }
    EnemyChargeState GetChargeState() const { return m_ChargeState; }
    bool IsCharging() const { return m_ChargeState == EnemyChargeState::Charging; }
    void BeginCharge();
    void CompleteChargeAttack();
    void BeginChargeShotTracking(bool heavyShot);
    std::optional<ChargeInterruptionResult> EndChargeShotTracking(
        float heavyInterruptDiameterMultiplier,
        float pierceInterruptMaxHpRatio);
    void CancelCharge(ChargeInterruptReason reason);
	int GetAnchorStacks() const { return m_AnchorStacks; }
	int& MutableAnchorStacks() { return m_AnchorStacks; }
	void ApplyLightweight(float multiplier)
	{
		if (!CanParticipateInCombat()) return;
		if (m_PendingKnockbackMultiplier <= 1.0f)
			m_PendingKnockbackMultiplier = (std::max)(1.0f, multiplier);
	}
	void BeginLightweightWindow()
	{
		if (!CanParticipateInCombat()) return;
		m_NextKnockbackMultiplier = m_PendingKnockbackMultiplier;
		m_PendingKnockbackMultiplier = 1.0f;
	}
	float GetNextKnockbackMultiplier() const { return m_NextKnockbackMultiplier; }
	void ConsumeLightweight()
	{
		if (CanParticipateInCombat()) m_NextKnockbackMultiplier = 1.0f;
	}
    bool IsArmorBoss() const { return m_EnemyData.id == "enemy_boss_core"; }
    const BossCombatRules::State& GetBossState() const { return m_BossState; }
    void SetDebugBossState(int armor, int breakShots)
    {
        if (!IsArmorBoss() || !CanParticipateInCombat()) return;
        m_BossState = {};
        m_BossState.shotsRemaining = std::clamp(breakShots, 0, BossCombatRules::BreakShots);
        m_BossState.armor = m_BossState.IsBroken() ? 0 : std::clamp(armor, 1, BossCombatRules::MaxArmor);
    }
    void BeginBossShot()
    {
        if (IsArmorBoss() && CanParticipateInCombat()) m_BossState.BeginShot();
    }
    void EndBossShot();
    void HitBreakBall(int ballId);
    int AdjustCollisionDamage(int damage, const DirectX::SimpleMath::Vector3& sourcePosition) const;
    void RegisterCollisionForStage(std::uintptr_t otherBallId);
    int GetCollisionStage() const { return m_CollisionStage; }
    float GetCollisionStageDamageMultiplier() const;
    int GetCollisionCountGraceTicks() const { return m_EnemyData.collisionCountGraceTicks; }
    const std::vector<float>& GetCollisionDamageMultipliers() const
    {
        return m_EnemyData.collisionDamageMultipliers;
    }
    bool HasCollisionStages() const { return !m_EnemyData.collisionDamageMultipliers.empty(); }
    int ApplyPocketDamage();
    float GetPocketDamageRatio() const { return m_EnemyData.pocketDamageRatio; }
    float GetFrontalDamageMultiplier() const { return m_EnemyData.frontalDamageMultiplier; }
    void OnPocketHit();
    void EnterPocketQueue(
        const DirectX::SimpleMath::Vector3& pocketEntryPosition);
    void ReturnFromPocket(
        const DirectX::SimpleMath::Vector3& position);
    void AdvanceTurnGimmicks();
    void ApplyStun(int turns)
    {
        if (CanParticipateInCombat())
            m_StunTurnsRemaining = (std::max)(m_StunTurnsRemaining, turns);
    }
    bool IsStunned() const { return m_StunTurnsRemaining > 0; }

    void SetStatus(const BallStatus& status)
    {
        if (CanParticipateInCombat()) m_Ball->SetStatus(status);
    }
    const BallStatus& GetStatus() const { return m_Ball->GetStatus(); }
    void SetHP(int hp)
    {
        if (CanParticipateInCombat()) m_Ball->SetHP(hp);
    }
    int GetHP() const { return m_Ball->GetHP(); }
    int GetMaxHP() const { return m_Ball->GetMaxHP(); }
    int GetAttack() const;
    int GetAttackBeforeBoardAbility() const
    {
        return CanParticipateInCombat() ? m_Ball->GetAttack() : 0;
    }
    int GetDefense() const { return m_Ball->GetDefense(); }
    void SetStatusEffects(const StatusEffectCollection& effects)
    {
        if (CanParticipateInCombat()) m_Ball->SetStatusEffects(effects);
    }
    const StatusEffectCollection& GetStatusEffects() const
    {
        return m_Ball->GetStatusEffects();
    }
    bool IsDefeated() const { return m_Ball->IsDefeated(); }
    bool IsPocketed() const { return m_IsPocketed; }
    DirectX::SimpleMath::Vector3 GetPocketEntryPosition() const
    {
        return m_PocketEntryPosition;
    }
    bool IsStopped() const { return m_Ball->IsStopped(); }
    float GetRadius() const { return m_Ball->GetRadius(); }
	// Presentation uses the radius of the rendered mesh. The physics radius can
	// be explicitly overridden by enemy data and must not drive an on-model UI.
	float GetVisualRadius() const;
    DirectX::SimpleMath::Vector3 GetVelocity() const { return m_Ball->GetVelocity(); }
    DirectX::SimpleMath::Vector3 GetPosition() const { return m_Ball->GetPosition(); }
    BallComponent* GetBall() const { return m_Ball; }
	// Presentation-only material contribution. It never changes transform,
	// radius, collision state, HP, or velocity.
	void SetPresentationHitFlash(float intensity);

    // -------------------------
    // 敵データ・ステータス
    // -------------------------
    const std::string& GetEnemyId() const { return m_EnemyData.id; }
    EnemyBoardAbilityType GetBoardAbilityType() const
    {
        return m_EnemyData.boardAbility.type;
    }
    float GetBoardAbilityRadius() const
    {
        return m_EnemyData.boardAbility.radius;
    }
    int GetNearbyLivingAllyCount() const
    {
        return (std::max)(0, m_NearbyLivingAllies);
    }
    int GetSwarmStage() const { return m_SwarmStage; }
	const std::array<int, 3>& GetSwarmAttackByStage() const
	{
		return m_EnemyData.boardAbility.swarmAttackByStage;
	}
	int GetBoardAbilityMaximumAttack() const
	{
		return m_EnemyData.boardAbility.maximumAttack;
	}
	float GetBrittleCollisionSpeed() const
	{
		return m_EnemyData.boardAbility.brittleCollisionSpeed;
	}
	int GetBrittleAttack() const
	{
		return m_EnemyData.boardAbility.brittleAttack;
	}
	int GetBrittleActionCount() const
	{
		return m_EnemyData.boardAbility.brittleActionCount;
	}
	int GetResonatorDamageReduction() const
	{
		return m_EnemyData.boardAbility.protectionDamageReduction;
	}
    bool IsBrittle() const { return m_BrittleActionsRemaining > 0; }
    int GetBrittleActionsRemaining() const
    {
        return m_BrittleActionsRemaining;
    }
    bool IsProtectedByResonator() const;
    int GetProtectionDamageReduction() const;
    void RefreshBoardAbilityState(bool notifyChange);
    void RegisterWallImpact(float collisionSpeed);
    void ConsumeBoardAbilityAfterAttack();
    void ApplyHotReloadData(const EnemyData& data);
    void ApplyStatusKeepHpRate(const BallStatus& status);

    int GetRewardMoney() const
    {
        return m_EnemyData.rewardMoney;
    }

    // -------------------------
    // デバッグUI
    // -------------------------
    void DrawImGui(const std::string& label);

private:
    void Draw(Camera* cam);
    void Uninit();
    void ApplyStatusValuesOnly(const BallStatus& status);
    void UpdateCollisionStageTint();
	void ApplyPresentationTint();
    void DrawNuisanceWarning();
    void DestroyNuisanceBall();
    void CompleteDefeatTransition();
    bool HasLiveNuisanceBall() const;
    void SpawnNuisanceBall();
    int CalculateDamageTakenWithoutProtection(int damage) const;
    void PublishBoardAbilityChange(
        const char* reason,
        int previousStage,
        int previousAttack,
        float collisionSpeed = 0.0f) const;

private:
    BallComponent* m_Ball = nullptr;
    BallRenderComponent* m_RenderComponent = nullptr;
    std::optional<EnemyData> m_InitialData;
    int m_CurrentFrame = 0;                                                                    // 経過フレーム
    EnemyData m_EnemyData;                                                                     // 敵データ
    DirectX::SimpleMath::Vector3 m_InitPosition = DirectX::SimpleMath::Vector3(50.0f, 0.0f, 50.0f); // 敵の初期位置
    bool m_IsPocketed = false;
    DirectX::SimpleMath::Vector3 m_PocketEntryPosition =
        DirectX::SimpleMath::Vector3::Zero;
    BossCombatRules::State m_BossState;
    DirectX::SimpleMath::Color m_BaseTint =
        DirectX::SimpleMath::Color(1.0f, 1.0f, 1.0f, 1.0f);
	float m_PresentationHitFlash = 0.0f;
    float m_DefeatVisualProgress = 0.0f;
    int m_CollisionStage = 0;
    std::unordered_map<std::uintptr_t, int> m_CollisionGraceByBall;
	int m_AnchorStacks = 0;
	float m_NextKnockbackMultiplier = 1.0f;
	float m_PendingKnockbackMultiplier = 1.0f;
    GameObject* m_NuisanceBallObject = nullptr;
    DirectX::SimpleMath::Vector3 m_NuisanceSpawnPosition = DirectX::SimpleMath::Vector3::Zero;
    int m_NuisanceTurnsUntilSpawn = 0;
    bool m_NuisanceWarningVisible = false;
    int m_StunTurnsRemaining = 0;
    int m_NearbyLivingAllies = -1;
    int m_SwarmStage = 0;
    int m_BrittleActionsRemaining = 0;
    int m_ProtectionState = -1;
    bool m_SkipNextGimmickAdvance = false;
    bool m_DefeatProcessed = false;
    EnemyActionType m_PlannedAction = EnemyActionType::Wait;
    int m_PlannedActionCost = 0;
    float m_PlannedChargeDamageMultiplier = 2.0f;
    EnemyChargeState m_ChargeState = EnemyChargeState::Normal;
    bool m_ChargeShotActive = false;
    bool m_ChargeShotHeavy = false;
    DirectX::SimpleMath::Vector3 m_ChargeShotStart =
        DirectX::SimpleMath::Vector3::Zero;
    int m_ChargeShotPierceDamage = 0;
    std::optional<ChargeInterruptionResult> m_PendingChargeInterruption;
    void ResetChargeShotTracking();
};
