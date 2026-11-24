#pragma once

#include "BallComponent.h"
#include "BattleTypes.h"
#include "EnemyData.h"
#include "BossCombatRules.h"
#include "BossEncounterRules.h"

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
    /// <summary>
    /// 敵球コンポーネントを既定の設定で構築する。
    /// </summary>
    EnemyBall() = default;
    /// <summary>
    /// 初期化の際に使う敵データを保持して敵球コンポーネントを構築する。
    /// </summary>
    explicit EnemyBall(const EnemyData& data) : m_InitialData(data) {}
    /// <summary>
    /// 敵球コンポーネントを既定の方法で破棄する。
    /// </summary>
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
    // Damage-defeated balls remain physical objects, but no longer
    // participate in combat-facing systems. Pocketed enemies are removed.
    /// <summary>
    /// 球コンポーネントがあり、未撃破かつ盤上にいて戦闘に参加できるか判定する。
    /// </summary>
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
    /// <summary>
    /// 予定された敵行動を返し、装甲ボスがブレイク中か行動中断時は待機として扱う。
    /// </summary>
    EnemyActionType GetPlannedAction() const
    {
        if (IsArmorBoss() && (m_BossState.IsBroken() || m_BossTurn.interruptedThisShot)) return EnemyActionType::Wait;
        return m_PlannedAction;
    }
    /// <summary>
    /// 予定された敵行動で消費する行動予算を返す。
    /// </summary>
    int GetPlannedActionCost() const { return m_PlannedActionCost; }
    /// <summary>
    /// 装甲ボスの攻撃値または予定されたチャージ攻撃に応じたダメージ倍率を返す。
    /// </summary>
    float GetPlannedDamageMultiplier() const
    {
        if (IsArmorBoss()) return BossEncounterRules::BossDamage(m_BossTurn, m_BossState) / 2.0f;
        return m_PlannedAction == EnemyActionType::ChargeAttack
            ? m_PlannedChargeDamageMultiplier
            : 1.0f;
    }
    /// <summary>
    /// 敵が戦闘に参加でき、データ上チャージを許可されているか判定する。
    /// </summary>
    bool CanUseCharge() const
    {
        return CanParticipateInCombat() && m_EnemyData.canCharge;
    }
    /// <summary>
    /// 敵球の現在のチャージ状態を返す。
    /// </summary>
    EnemyChargeState GetChargeState() const { return m_ChargeState; }
    /// <summary>
    /// 敵球がチャージ中かどうかを返す。
    /// </summary>
    bool IsCharging() const { return m_ChargeState == EnemyChargeState::Charging; }
    void BeginCharge();
    void CompleteChargeAttack();
    void BeginChargeShotTracking(bool heavyShot);
    std::optional<ChargeInterruptionResult> EndChargeShotTracking(
        float heavyInterruptDiameterMultiplier,
        float pierceInterruptMaxHpRatio);
    void CancelCharge(ChargeInterruptReason reason);
	/// <summary>
	/// 攻撃間隔を1ターン以上に設定し、次の攻撃までの残りターン数を初期化する。
	/// </summary>
	void InitializeAttackTimer(int interval)
	{
		m_AttackInterval = (std::max)(1, interval);
		m_TurnsUntilAttack = m_AttackInterval;
	}
	/// <summary>
	/// 敵球が攻撃する間隔をターン数で返す。
	/// </summary>
	int GetAttackInterval() const { return m_AttackInterval; }
	/// <summary>
	/// 敵球が次に攻撃できるまでの残りターン数を返す。
	/// </summary>
	int GetTurnsUntilAttack() const { return m_TurnsUntilAttack; }
	/// <summary>
	/// 戦闘に参加できる敵の攻撃待ちを1ターン進め、攻撃可能になったか判定する。
	/// </summary>
	bool AdvanceAttackTimer()
	{
		if (!CanParticipateInCombat()) return false;
		m_TurnsUntilAttack = (std::max)(0, m_TurnsUntilAttack - 1);
		return m_TurnsUntilAttack == 0;
	}
	/// <summary>
	/// 次の攻撃までの残りターン数を、設定された攻撃間隔に戻す。
	/// </summary>
	void ResetAttackTimer() { m_TurnsUntilAttack = m_AttackInterval; }
	/// <summary>
	/// この敵球に付いているアンカーのスタック数を返す。
	/// </summary>
	int GetAnchorStacks() const { return m_AnchorStacks; }
	/// <summary>
	/// 敵球のアンカースタック数を直接更新できる参照を返す。
	/// </summary>
	int& MutableAnchorStacks() { return m_AnchorStacks; }
	/// <summary>
	/// 有効な敵球に次回の軽量化効果が未予約の場合、ノックバック倍率を予約する。
	/// </summary>
	void ApplyLightweight(float multiplier)
	{
		if (!CanParticipateInCombat()) return;
		if (m_PendingKnockbackMultiplier <= 1.0f)
			m_PendingKnockbackMultiplier = (std::max)(1.0f, multiplier);
	}
	/// <summary>
	/// 予約していたノックバック倍率を現在の軽量化効果に移し、予約を消去する。
	/// </summary>
	void BeginLightweightWindow()
	{
		if (!CanParticipateInCombat()) return;
		m_NextKnockbackMultiplier = m_PendingKnockbackMultiplier;
		m_PendingKnockbackMultiplier = 1.0f;
	}
	/// <summary>
	/// 次のノックバックに使う現在の軽量化倍率を返す。
	/// </summary>
	float GetNextKnockbackMultiplier() const { return m_NextKnockbackMultiplier; }
	/// <summary>
	/// 次に有効になるよう予約しているノックバック倍率を返す。
	/// </summary>
	float GetPendingKnockbackMultiplier() const { return m_PendingKnockbackMultiplier; }
	/// <summary>
	/// 戦闘に参加できる敵球の現在の軽量化効果を消費する。
	/// </summary>
	void ConsumeLightweight()
	{
		if (CanParticipateInCombat()) m_NextKnockbackMultiplier = 1.0f;
	}
    /// <summary>
    /// 敵識別子が装甲ボスのものに一致するか判定する。
    /// </summary>
    bool IsArmorBoss() const { return m_EnemyData.id == "enemy_boss_core"; }
    /// <summary>
    /// 敵識別子が装甲ボスの取り巻きのものに一致するか判定する。
    /// </summary>
    bool IsBossMinion() const { return m_EnemyData.id == BossEncounterRules::MinionId; }
    /// <summary>
    /// ボスの取り巻きを個別に識別する番号を返す。
    /// </summary>
    int GetBossMinionSerial() const { return m_BossMinionSerial; }
    /// <summary>
    /// ボスの取り巻きを個別に識別する番号を設定する。
    /// </summary>
    void SetBossMinionSerial(int serial) { m_BossMinionSerial = serial; }
    /// <summary>
    /// この敵球が保持する初期値や能力を含む敵データを返す。
    /// </summary>
    const EnemyData& GetEnemyData() const { return m_EnemyData; }
    /// <summary>
    /// 装甲ボスのターン状態を更新できる参照を返す。
    /// </summary>
    BossEncounterRules::TurnState& BossTurn() { return m_BossTurn; }
    /// <summary>
    /// 装甲ボスのターン状態への読み取り専用の参照を返す。
    /// </summary>
    const BossEncounterRules::TurnState& BossTurn() const { return m_BossTurn; }
    void ConsumeBossMinion(EnemyBall& boss);
    void RetireBossMinion();
    /// <summary>
    /// 装甲とブレイクの残りショット数を含むボスの戦闘状態を返す。
    /// </summary>
    const BossCombatRules::State& GetBossState() const { return m_BossState; }
    /// <summary>
    /// 有効な装甲ボスについて、デバッグ用に装甲とブレイク残りショット数を設定する。
    /// </summary>
    void SetDebugBossState(int armor, int breakShots)
    {
        if (!IsArmorBoss() || !CanParticipateInCombat()) return;
        m_BossState = {};
        m_BossState.shotsRemaining = std::clamp(breakShots, 0, BossCombatRules::BreakShots);
        m_BossState.armor = m_BossState.IsBroken() ? 0 : std::clamp(armor, 1, BossCombatRules::MaxArmor);
    }
    /// <summary>
    /// 装甲ボスの新しいショットを開始し、ショット中の行動中断フラグを初期化する。
    /// </summary>
    void BeginBossShot()
    {
        if (IsArmorBoss() && CanParticipateInCombat()) { m_BossState.BeginShot(); m_BossTurn.interruptedThisShot = false; }
    }
    void EndBossShot();
    void HitBreakBall(int ballId);
    BossCombatRules::PocketResult ResolveBossPocket();
    int AdjustCollisionDamage(int damage, const DirectX::SimpleMath::Vector3& sourcePosition) const;
    void RegisterCollisionForStage(std::uintptr_t otherBallId);
    /// <summary>
    /// 衝突によって進む敵球の現在のダメージ段階を返す。
    /// </summary>
    int GetCollisionStage() const { return m_CollisionStage; }
    float GetCollisionStageDamageMultiplier() const;
    /// <summary>
    /// 同じ相手との衝突を再集計できるようになるまでの固定更新数を返す。
    /// </summary>
    int GetCollisionCountGraceTicks() const { return m_EnemyData.collisionCountGraceTicks; }
    /// <summary>
    /// 衝突段階ごとに設定された被ダメージ倍率の一覧を返す。
    /// </summary>
    const std::vector<float>& GetCollisionDamageMultipliers() const
    {
        return m_EnemyData.collisionDamageMultipliers;
    }
    /// <summary>
    /// 衝突数に応じたダメージ段階が敵データに定義されているかを返す。
    /// </summary>
    bool HasCollisionStages() const { return !m_EnemyData.collisionDamageMultipliers.empty(); }
    int ApplyPocketDamage();
    /// <summary>
    /// ポケットダメージの計算に使う最大HPに対する比率を返す。
    /// </summary>
    float GetPocketDamageRatio() const { return m_EnemyData.pocketDamageRatio; }
    /// <summary>
    /// 正面方向からの衝突ダメージに使う倍率を返す。
    /// </summary>
    float GetFrontalDamageMultiplier() const { return m_EnemyData.frontalDamageMultiplier; }
    void OnPocketHit();
    void AdvanceTurnGimmicks();
    /// <summary>
    /// 敵が戦闘に参加できる場合に、既存の残りより長いスタン期間を適用する。
    /// </summary>
    void ApplyStun(int turns)
    {
        if (CanParticipateInCombat())
            m_StunTurnsRemaining = (std::max)(m_StunTurnsRemaining, turns);
    }
    /// <summary>
    /// スタンの残りターン数があるかを返す。
    /// </summary>
    bool IsStunned() const { return m_StunTurnsRemaining > 0; }

    /// <summary>
    /// 戦闘に参加できる敵球に、指定した球のステータスを適用する。
    /// </summary>
    void SetStatus(const BallStatus& status)
    {
        if (CanParticipateInCombat()) m_Ball->SetStatus(status);
    }
    /// <summary>
    /// 球コンポーネントが保持する現在のステータスを返す。
    /// </summary>
    const BallStatus& GetStatus() const { return m_Ball->GetStatus(); }
    /// <summary>
    /// 戦闘に参加できる敵球の現在HPを設定する。
    /// </summary>
    void SetHP(int hp)
    {
        if (CanParticipateInCombat()) m_Ball->SetHP(hp);
    }
    /// <summary>
    /// 敵球の現在HPを返す。
    /// </summary>
    int GetHP() const { return m_Ball->GetHP(); }
    /// <summary>
    /// 敵球の最大HPを返す。
    /// </summary>
    int GetMaxHP() const { return m_Ball->GetMaxHP(); }
    int GetAttack() const;
    /// <summary>
    /// 戦闘参加中なら盤面能力を加える前の攻撃力を返し、参加できなければ0を返す。
    /// </summary>
    int GetAttackBeforeBoardAbility() const
    {
        return CanParticipateInCombat() ? m_Ball->GetAttack() : 0;
    }
    /// <summary>
    /// 敵球の現在の防御力を返す。
    /// </summary>
    int GetDefense() const { return m_Ball->GetDefense(); }
    /// <summary>
    /// 戦闘に参加できる敵球に状態効果の一覧を設定する。
    /// </summary>
    void SetStatusEffects(const StatusEffectCollection& effects)
    {
        if (CanParticipateInCombat()) m_Ball->SetStatusEffects(effects);
    }
    /// <summary>
    /// 球コンポーネントが保持する状態効果の一覧を返す。
    /// </summary>
    const StatusEffectCollection& GetStatusEffects() const
    {
        return m_Ball->GetStatusEffects();
    }
    /// <summary>
    /// 球コンポーネントが撃破状態かどうかを返す。
    /// </summary>
    bool IsDefeated() const { return m_Ball->IsDefeated(); }
    /// <summary>
    /// 敵球がポケットに入って盤面から除外された状態かを返す。
    /// </summary>
    bool IsPocketed() const { return m_IsPocketed; }
    /// <summary>
    /// 敵球がポケットに入ったときに記録した位置を返す。
    /// </summary>
    DirectX::SimpleMath::Vector3 GetPocketEntryPosition() const
    {
        return m_PocketEntryPosition;
    }
    /// <summary>
    /// 球コンポーネントが停止状態かどうかを返す。
    /// </summary>
    bool IsStopped() const { return m_Ball->IsStopped(); }
    /// <summary>
    /// 敵球の衝突判定に使う現在の半径を返す。
    /// </summary>
    float GetRadius() const { return m_Ball->GetRadius(); }
	// Presentation uses the radius of the rendered mesh. The physics radius can
	// be explicitly overridden by enemy data and must not drive an on-model UI.
	float GetVisualRadius() const;
    /// <summary>
    /// 敵球の現在の物理速度を返す。
    /// </summary>
    DirectX::SimpleMath::Vector3 GetVelocity() const { return m_Ball->GetVelocity(); }
    /// <summary>
    /// 敵球の現在のワールド座標を返す。
    /// </summary>
    DirectX::SimpleMath::Vector3 GetPosition() const { return m_Ball->GetPosition(); }
    /// <summary>
    /// この敵球を構成する球コンポーネントへのポインターを返す。
    /// </summary>
    BallComponent* GetBall() const { return m_Ball; }
	// Presentation-only material contribution. It never changes transform,
	// radius, collision state, HP, or velocity.
	void SetPresentationHitFlash(float intensity);

    // -------------------------
    // 敵データ・ステータス
    // -------------------------
    /// <summary>
    /// 敵データ上の識別文字列を返す。
    /// </summary>
    const std::string& GetEnemyId() const { return m_EnemyData.id; }
    /// <summary>
    /// 敵が盤面で発揮する能力の種類を返す。
    /// </summary>
    EnemyBoardAbilityType GetBoardAbilityType() const
    {
        return m_EnemyData.boardAbility.type;
    }
    /// <summary>
    /// 敵の盤面能力が届く範囲の半径を返す。
    /// </summary>
    float GetBoardAbilityRadius() const
    {
        return m_EnemyData.boardAbility.radius;
    }
    /// <summary>
    /// 直近に数えた近くの生存味方数を0以上で返す。
    /// </summary>
    int GetNearbyLivingAllyCount() const
    {
        return (std::max)(0, m_NearbyLivingAllies);
    }
    /// <summary>
    /// 近くの生存味方数から求めた現在の群体強化段階を返す。
    /// </summary>
    int GetSwarmStage() const { return m_SwarmStage; }
	/// <summary>
	/// 群体強化の各段階に設定されている攻撃力を返す。
	/// </summary>
	const std::array<int, 3>& GetSwarmAttackByStage() const
	{
		return m_EnemyData.boardAbility.swarmAttackByStage;
	}
	/// <summary>
	/// 群体強化に設定されている攻撃力の上限を返す。
	/// </summary>
	int GetBoardAbilityMaximumAttack() const
	{
		return m_EnemyData.boardAbility.maximumAttack;
	}
	/// <summary>
	/// 壁衝突で脆化が発動するために必要な衝突速度を返す。
	/// </summary>
	float GetBrittleCollisionSpeed() const
	{
		return m_EnemyData.boardAbility.brittleCollisionSpeed;
	}
	/// <summary>
	/// 脆化中に使う攻撃力の設定値を返す。
	/// </summary>
	int GetBrittleAttack() const
	{
		return m_EnemyData.boardAbility.brittleAttack;
	}
	/// <summary>
	/// 脆化の発動時に設定される行動回数を返す。
	/// </summary>
	int GetBrittleActionCount() const
	{
		return m_EnemyData.boardAbility.brittleActionCount;
	}
	/// <summary>
	/// 防護共鳴の範囲内にいる味方に適用するダメージ軽減量を返す。
	/// </summary>
	int GetResonatorDamageReduction() const
	{
		return m_EnemyData.boardAbility.protectionDamageReduction;
	}
    /// <summary>
    /// 脆化が残っているかを返す。
    /// </summary>
    bool IsBrittle() const { return m_BrittleActionsRemaining > 0; }
    /// <summary>
    /// 脆化が残る敵の行動回数を返す。
    /// </summary>
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

    /// <summary>
    /// この敵を撃破したときの所持金報酬を返す。
    /// </summary>
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
    BossEncounterRules::TurnState m_BossTurn;
    int m_BossMinionSerial = -1;
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
    int m_AttackInterval = 2;
    int m_TurnsUntilAttack = 2;
    void ResetChargeShotTracking();
};
