#include "EnemyBall.h"
#include "BallPhysicsRules.h"
#include "EnemyChargeRules.h"
#include "BallMechanics.h"
#include "EnemyGimmickRules.h"
#include "FixedStepClock.h"

#include "Camera.h"
#include "BallRenderComponent.h"
#include "BalanceLogger.h"
#include "GameBattleEvents.h"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "Ground.h"
#include "PlayerBall.h"
#include "BallFactory.h"
#include "NuisanceBall.h"
#include "TableConfig.h"
#include "imgui/imgui.h"
#include "GameObject.h"

#include <iostream>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    constexpr float kDefeatVisualTransitionSeconds = 0.3f;
}

void EnemyBall::Awake()
{
    GameObject* owner = GetGameObject();
    if (owner == nullptr)
    {
        return;
    }

    m_Ball = owner->GetComponent<BallComponent>();
    m_RenderComponent = owner->GetComponent<BallRenderComponent>();
    if (m_Ball == nullptr || m_RenderComponent == nullptr)
    {
        owner->Destroy();
        return;
    }

    m_Ball->SetPocketHandler([this]() { OnPocketHit(); });

    if (m_InitialData.has_value())
    {
        Init(*m_InitialData);
        m_InitialData.reset();
    }
    else
    {
        Init();
    }
}

void EnemyBall::Draw()
{
    Draw(&GameRuntime::MainCamera());
}

float EnemyBall::GetVisualRadius() const
{
	if (m_Ball == nullptr)
	{
		return 0.0f;
	}
	if (m_RenderComponent == nullptr)
	{
		return GetRadius();
	}
	const Vector3 scale = m_Ball->GetScale();
	const float maximumScale = (std::max)({
		std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) });
	const float visualRadius =
		m_RenderComponent->GetModelBaseRadius() * maximumScale;
	return visualRadius > 0.001f ? visualRadius : GetRadius();
}

void EnemyBall::OnDestroy()
{
    m_NuisanceWarningVisible = false;
    DestroyNuisanceBall();
    if (m_Ball != nullptr)
    {
        m_Ball->SetPocketHandler({});
    }
    Uninit();
}

void EnemyBall::Init()
{
    // デフォルトのEnemyDataを使って初期化する
    EnemyData data;
    Init(data);
}

void EnemyBall::Init(const EnemyData& data)
{
	m_AnchorStacks = 0;
	m_DefeatProcessed = false;
	m_NextKnockbackMultiplier = 1.0f;
	m_PendingKnockbackMultiplier = 1.0f;
    m_BossState = {};
    m_IsPocketed = false;
    m_PocketEntryPosition = Vector3::Zero;
    m_CollisionStage = 0;
    m_CollisionGraceByBall.clear();
    m_NuisanceBallObject = nullptr;
    m_StunTurnsRemaining = 0;
    m_NearbyLivingAllies = -1;
    m_SwarmStage = 0;
    m_BrittleActionsRemaining = 0;
    m_ProtectionState = -1;
    m_SkipNextGimmickAdvance = false;
    m_PlannedAction = EnemyActionType::Wait;
    m_PlannedActionCost = 0;
    m_PlannedChargeDamageMultiplier = 2.0f;
    m_ChargeState = EnemyChargeState::Normal;
    ResetChargeShotTracking();
    m_PendingChargeInterruption.reset();
	m_PresentationHitFlash = 0.0f;
    m_DefeatVisualProgress = 0.0f;
    m_EnemyData = data;                        // 読み込んだ敵データを保持

	m_Ball->ResetDefeated();
    m_Ball->SetMaxHP(m_EnemyData.maxHp);
    SetStatus(m_EnemyData.status);             // ステータスを反映
    SetStatusEffects(m_EnemyData.initialStatusEffects);

    m_RenderComponent->LoadModel(
        m_EnemyData.modelFilePath.c_str(),
        m_EnemyData.textureDirectory.c_str()
    );

    Color enemyTint(0.95f, 0.18f, 0.16f, 1.0f);
    if (m_EnemyData.id == "enemy_strong")
    {
        enemyTint = Color(1.0f, 0.34f, 0.10f, 1.0f);
    }
    else if (m_EnemyData.id == "enemy_tank")
    {
        enemyTint = Color(0.66f, 0.20f, 0.82f, 1.0f);
    }
    else if (m_EnemyData.id == "enemy_striker")
    {
        enemyTint = Color(1.0f, 0.58f, 0.08f, 1.0f);
    }
    else if (m_EnemyData.boardAbility.type ==
        EnemyBoardAbilityType::SwarmAmplifier)
    {
        enemyTint = Color(0.98f, 0.30f, 0.58f, 1.0f);
    }
    else if (m_EnemyData.boardAbility.type ==
        EnemyBoardAbilityType::ImpactBrittle)
    {
        enemyTint = Color(1.0f, 0.55f, 0.18f, 1.0f);
    }
    else if (m_EnemyData.boardAbility.type ==
        EnemyBoardAbilityType::ProtectionResonator)
    {
        enemyTint = Color(0.20f, 0.82f, 0.92f, 1.0f);
    }
    else if (m_EnemyData.id == "enemy_boss_core")
    {
        enemyTint = Color(0.92f, 0.08f, 0.42f, 1.0f);
    }
    if (m_EnemyData.frontalDamageMultiplier < 1.0f) enemyTint = Color(0.15f, 0.7f, 0.9f, 1.0f);
    else if (m_EnemyData.pocketDamageRatio > 0.0f) enemyTint = Color(0.3f, 0.85f, 0.4f, 1.0f);
    else if (m_EnemyData.nuisanceBall.enabled) enemyTint = Color(0.72f, 0.22f, 0.95f, 1.0f);
    else if (!m_EnemyData.collisionDamageMultipliers.empty()) enemyTint = Color(0.95f, 0.72f, 0.18f, 1.0f);
    m_BaseTint = enemyTint;
    UpdateCollisionStageTint();

    m_Ball->SetPosition(m_EnemyData.initPosition); // 初期位置を反映
    m_Ball->SetScale(m_EnemyData.scale);           // スケールを反映
    m_Ball->UpdateRadius();                                  // スケールに合わせて半径を更新

    std::vector<Ground*> grounds =
        GameRuntime::World().GetComponents<Ground>();
    if (!grounds.empty())
    {
        // Groundがある場合は、台の高さに合わせてY座標を補正する
        Vector3 position = m_Ball->GetPosition();
        position.y = grounds[0]->GetFieldHeight();
        m_Ball->SetPosition(position);
    }

    m_Ball->GetMutableVelocity() = Vector3::Zero;             // 速度を初期化
    m_Ball->GetMutableAcceleration() = Vector3::Zero;             // 加速度を初期化
    m_CurrentFrame = 0;                         // フレームカウントを初期化

    m_NuisanceSpawnPosition = m_EnemyData.initPosition +
        m_EnemyData.nuisanceBall.spawnOffset;
    m_NuisanceSpawnPosition.y = TableConfig::FIELD_HEIGHT;
    const float nuisanceMargin = m_EnemyData.nuisanceBall.radius + 1.0f;
    m_NuisanceSpawnPosition.x = std::clamp(
        m_NuisanceSpawnPosition.x,
        -TableConfig::GetFieldWidth() * 0.5f + nuisanceMargin,
        TableConfig::GetFieldWidth() * 0.5f - nuisanceMargin);
    m_NuisanceSpawnPosition.z = std::clamp(
        m_NuisanceSpawnPosition.z,
        -TableConfig::GetFieldDepth() * 0.5f + nuisanceMargin,
        TableConfig::GetFieldDepth() * 0.5f - nuisanceMargin);
    m_NuisanceTurnsUntilSpawn = m_EnemyData.nuisanceBall.enabled
        ? (std::max)(1, m_EnemyData.nuisanceBall.initialDelayTurns)
        : 0;
    m_NuisanceWarningVisible = m_NuisanceTurnsUntilSpawn == 1;
}

void EnemyBall::FixedUpdate()
{
    if (m_Ball == nullptr || m_IsPocketed)
    {
        return;
    }

    m_CurrentFrame++;

    if (IsDefeated())
    {
        const float step = static_cast<float>(FixedStepClock::StepSeconds) /
            kDefeatVisualTransitionSeconds;
        m_DefeatVisualProgress = (std::min)(
            1.0f,
            m_DefeatVisualProgress + step);
    }
    else
    {
        m_DefeatVisualProgress = 0.0f;
    }

    for (auto it = m_CollisionGraceByBall.begin();
        it != m_CollisionGraceByBall.end();)
    {
        if (--it->second <= 0) it = m_CollisionGraceByBall.erase(it);
        else ++it;
    }

    BallPhysicsRules::EnemyFriction(m_Ball->GetMutableVelocity(), m_Ball->GetMutableFriction());

    // Movement is performed after EVERY ball has applied friction for this tick.
}

void EnemyBall::Draw(Camera* cam)
{
	if (m_RenderComponent == nullptr && GetGameObject() != nullptr)
		m_RenderComponent = GetGameObject()->GetComponent<BallRenderComponent>();
	if (cam == nullptr || m_Ball == nullptr || m_RenderComponent == nullptr ||
		m_IsPocketed)
    {
        return;
    }

    cam->SetCamera();

    m_RenderComponent->SetDefeatVisualProgress(m_DefeatVisualProgress);
    m_RenderComponent->BeginDraw();

    // 行列の作成（BallComponentが計算した m_Ball->GetMutableRollingRotation() を適用）
    Matrix rDirection = Matrix::CreateFromQuaternion(
        m_Ball->GetRotation());
    Matrix rRolling = Matrix::CreateFromQuaternion(m_Ball->GetMutableRollingRotation());
    Matrix r = rDirection * rRolling;

    Matrix t = Matrix::CreateTranslation(m_Ball->GetPosition());
    Matrix s = Matrix::CreateScale(m_Ball->GetScale());

    Matrix worldmtx = s * r * t;

    // BallComponentの共通描画関数を呼び出す
    m_RenderComponent->DrawMesh(worldmtx);
    DrawNuisanceWarning();
}

void EnemyBall::Uninit()
{
    // 必要に応じた解放処理
}

void EnemyBall::Defeat()
{
    if (m_Ball == nullptr)
    {
        return;
    }

    const Vector3 velocity = m_Ball->GetMutableVelocity();
    const Vector3 acceleration = m_Ball->GetMutableAcceleration();
    m_Ball->Defeat();
    // BallComponent has a generic defeat stop for actors that leave play.
    // Enemy corpses stay on the table, so preserve the exact physical state.
    m_Ball->GetMutableVelocity() = velocity;
    m_Ball->GetMutableAcceleration() = acceleration;
    CompleteDefeatTransition();
}

void EnemyBall::CompleteDefeatTransition()
{
    if (m_Ball == nullptr || !m_Ball->IsDefeated() || m_DefeatProcessed)
    {
        return;
    }

    m_DefeatProcessed = true;
    m_DefeatVisualProgress = 0.0f;
    CancelCharge(ChargeInterruptReason::Defeated);
    m_PendingChargeInterruption.reset();
    m_PlannedAction = EnemyActionType::Wait;
    m_PlannedActionCost = 0;
    m_ChargeState = EnemyChargeState::Normal;
    ResetChargeShotTracking();
    m_StunTurnsRemaining = 0;
    m_BrittleActionsRemaining = 0;
    m_NearbyLivingAllies = 0;
    m_SwarmStage = 0;
    m_ProtectionState = 0;
    m_CollisionStage = 0;
    m_CollisionGraceByBall.clear();
    m_NextKnockbackMultiplier = 1.0f;
    m_PendingKnockbackMultiplier = 1.0f;
    m_Ball->SetStatusEffects({});
    m_Ball->SetAuraStatusEffects({});
    UpdateCollisionStageTint();
    m_NuisanceWarningVisible = false;
    DestroyNuisanceBall();

    GameBattleEvents::EnemyDefeated(this);

    // Remove this ball as a protection/swarm source immediately. The ball
    // itself remains in the physics world and is still refreshed by rendering.
    for (EnemyBall* enemy : GameRuntime::World().GetComponents<EnemyBall>())
    {
        if (enemy != nullptr && enemy != this && enemy->CanParticipateInCombat())
        {
            enemy->RefreshBoardAbilityState(true);
        }
    }
}

void EnemyBall::RemoveFromFieldAfterPocket()
{
    if (m_Ball == nullptr)
    {
        return;
    }

    m_Ball->GetMutableVelocity() = Vector3::Zero;
    m_Ball->GetMutableAcceleration() = Vector3::Zero;
    if (GameObject* owner = GetGameObject())
    {
        owner->SetActive(false);
    }
}

void EnemyBall::ApplyHotReloadData(const EnemyData& data)
{
    // 別の敵IDのデータを誤って適用しない
    if (m_EnemyData.id != data.id || !CanParticipateInCombat())
    {
        return;
    }

    // 敵データを更新
    m_EnemyData.status = data.status;
    m_EnemyData.maxHp = data.maxHp;
    m_EnemyData.canCharge = data.canCharge;
    m_EnemyData.frontalDamageMultiplier = data.frontalDamageMultiplier;
    m_EnemyData.pocketDamageRatio = data.pocketDamageRatio;
    m_EnemyData.collisionDamageMultipliers = data.collisionDamageMultipliers;
    m_EnemyData.collisionCountGraceTicks = data.collisionCountGraceTicks;
    m_EnemyData.nuisanceBall = data.nuisanceBall;
    m_EnemyData.boardAbility = data.boardAbility;
    m_EnemyData.rewardMoney = data.rewardMoney;
    m_EnemyData.rewardExp = data.rewardExp;
    m_EnemyData.scale = data.scale;
    m_EnemyData.initialStatusEffects = data.initialStatusEffects;

    // HP割合を維持したままステータス更新
    ApplyStatusKeepHpRate(data.status);
    SetStatusEffects(data.initialStatusEffects);
    UpdateCollisionStageTint();

    // スケールも反映
    m_Ball->SetScale(data.scale);
    m_Ball->UpdateRadius();

    std::cout << "[HotReload] Enemy updated: "
        << m_EnemyData.id
        << " HP: " << m_EnemyData.maxHp
        << " Attack: " << m_EnemyData.status.attack
        << " Defense: " << m_EnemyData.status.defense
        << std::endl;
}

void EnemyBall::ApplyStatusKeepHpRate(const BallStatus& status)
{
    if (!CanParticipateInCombat()) return;

    float hpRate = 1.0f;                       // 現在HPの割合

    if (GetMaxHP() > 0)
    {
        hpRate = static_cast<float>(GetHP()) / static_cast<float>(GetMaxHP());
    }

    m_Ball->SetMaxHP(m_EnemyData.maxHp);
    ApplyStatusValuesOnly(status);

    int newHp = static_cast<int>(GetMaxHP() * hpRate); // 更新後の最大HPに合わせた現在HP

    if (newHp < 1)
    {
        newHp = 1;
    }

    if (newHp > GetMaxHP())
    {
        newHp = GetMaxHP();
    }

    SetHP(newHp);
}

void EnemyBall::ApplyStatusValuesOnly(const BallStatus& status)
{
    if (!CanParticipateInCombat()) return;
    // HPや撃破状態を初期化せず、ステータス構造体だけを差し替える
    m_Ball->ApplyStatusValuesOnly(status);
}

void EnemyBall::OnPocketHit()
{
    if (m_Ball == nullptr || m_IsPocketed)
    {
        return;
    }
    if (!IsArmorBoss()) GameBattleEvents::EnemyPocketed(this);
}

void EnemyBall::EnterPocketQueue(const Vector3& pocketEntryPosition)
{
    if (m_Ball == nullptr || IsDefeated() || m_IsPocketed)
    {
        return;
    }
    CancelCharge(ChargeInterruptReason::Pocketed);
    m_IsPocketed = true;
    m_PocketEntryPosition = pocketEntryPosition;
    m_SkipNextGimmickAdvance = true;
    m_NuisanceWarningVisible = false;
    if (m_EnemyData.nuisanceBall.enabled)
    {
        m_NuisanceTurnsUntilSpawn = (std::max)(1, m_EnemyData.nuisanceBall.respawnDelayTurns);
    }
    m_Ball->ResetAtPosition(Vector3(0.0f, -1000.0f, 0.0f));
    if (GameObject* owner = GetGameObject())
    {
        owner->SetActive(false);
    }
}

void EnemyBall::ReturnFromPocket(const Vector3& position)
{
    if (m_Ball == nullptr || IsDefeated())
    {
        return;
    }
    m_Ball->ResetAtPosition(position);
    m_IsPocketed = false;
    m_NuisanceSpawnPosition = position + m_EnemyData.nuisanceBall.spawnOffset;
    m_NuisanceSpawnPosition.y = TableConfig::FIELD_HEIGHT;
    const float nuisanceMargin = m_EnemyData.nuisanceBall.radius + 1.0f;
    m_NuisanceSpawnPosition.x = std::clamp(
        m_NuisanceSpawnPosition.x,
        -TableConfig::GetFieldWidth() * 0.5f + nuisanceMargin,
        TableConfig::GetFieldWidth() * 0.5f - nuisanceMargin);
    m_NuisanceSpawnPosition.z = std::clamp(
        m_NuisanceSpawnPosition.z,
        -TableConfig::GetFieldDepth() * 0.5f + nuisanceMargin,
        TableConfig::GetFieldDepth() * 0.5f - nuisanceMargin);
    m_NuisanceWarningVisible =
        m_EnemyData.nuisanceBall.enabled &&
        !HasLiveNuisanceBall() &&
        m_NuisanceTurnsUntilSpawn == 1;
    if (GameObject* owner = GetGameObject())
    {
        owner->SetActive(true);
    }
}

void EnemyBall::TakeDamage(
    int damage,
    EnemyDamageKind damageKind)
{
	if (!CanParticipateInCombat())
	{
		return;
	}
	const int hpBefore = m_Ball->GetHP();
	const bool wasDefeated = m_Ball->IsDefeated();
	const Vector3 collisionVelocity =
		m_Ball->GetMutableVelocity();
	const Vector3 collisionAcceleration =
		m_Ball->GetMutableAcceleration();
	const int damageBeforeProtection =
		CalculateDamageTakenWithoutProtection(damage);
	const int resolvedDamage = CalculateDamageTaken(damage);
	m_Ball->DamageAfterDefense(resolvedDamage);
	const int appliedDamage = (std::max)(0, hpBefore - m_Ball->GetHP());
	const int protectionPrevented = (std::max)(
		0,
		damageBeforeProtection - resolvedDamage);
	if (protectionPrevented > 0)
	{
		BalanceLogger::GetInstance().RecordEvent(
			"enemy_protection_damage_reduced",
			{
				{ "enemy_id", m_EnemyData.id },
				{ "prevented_damage", protectionPrevented },
				{ "damage_before_protection", damageBeforeProtection },
				{ "damage_after_protection", resolvedDamage },
			});
	}
	if (m_ChargeShotActive)
	{
		m_ChargeShotPierceDamage =
			EnemyChargeRules::AccumulateAppliedDamage(
				m_ChargeShotPierceDamage,
				damageKind,
				appliedDamage);
	}

	// 通常ダメージで倒れた敵は、全ボールが止まるまで表示と物理判定を残す。
	// ポケットによる除外は別処理とし、即座に反映する。
	if (!wasDefeated && m_Ball->IsDefeated())
	{
		m_Ball->GetMutableVelocity() = collisionVelocity;
		m_Ball->GetMutableAcceleration() = collisionAcceleration;
		CompleteDefeatTransition();
	}

	BalanceLogger::GetInstance().RecordEnemyDamage(
		m_EnemyData.id,
		appliedDamage);
}

int EnemyBall::CalculateDamageTaken(int damage) const
{
	const int damageAfterDefense = CalculateDamageTakenWithoutProtection(damage);
	if (damageAfterDefense <= 0) return 0;
	return EnemyBoardAbilityRules::ApplyProtectionReduction(
		damageAfterDefense,
		GetProtectionDamageReduction());
}

int EnemyBall::CalculateDamageTakenWithoutProtection(int damage) const
{
	if (m_Ball == nullptr || m_Ball->IsDefeated() || damage <= 0) return 0;
	if (IsArmorBoss())
	{
		damage = BossCombatRules::DirectDamage(
			damage,
			GetDefense(),
			m_BossState) + GetDefense();
	}
	return m_Ball->CalculateDamageTaken(damage);
}

int EnemyBall::GetAttack() const
{
	if (!CanParticipateInCombat()) return 0;
	const int statusModifier =
		m_Ball->GetAttack() - m_EnemyData.status.attack;
	int attack = m_EnemyData.status.attack;
	if (m_EnemyData.boardAbility.type ==
		EnemyBoardAbilityType::SwarmAmplifier)
	{
		attack = EnemyBoardAbilityRules::SwarmAttack(
			GetNearbyLivingAllyCount(),
			m_EnemyData.boardAbility.swarmAttackByStage,
			m_EnemyData.boardAbility.maximumAttack);
	}
	else if (m_EnemyData.boardAbility.type ==
			EnemyBoardAbilityType::ImpactBrittle &&
		IsBrittle())
	{
		attack = m_EnemyData.boardAbility.brittleAttack;
	}
	return (std::max)(0, attack + statusModifier);
}

bool EnemyBall::IsProtectedByResonator() const
{
	if (m_Ball == nullptr || IsDefeated() || IsPocketed()) return false;
	for (const EnemyBall* source :
		GameRuntime::World().GetComponents<EnemyBall>())
	{
		if (source == nullptr || source == this || source->IsDefeated() ||
			source->IsPocketed() ||
			source->GetBoardAbilityType() !=
				EnemyBoardAbilityType::ProtectionResonator)
		{
			continue;
		}
		Vector3 delta = source->GetPosition() - GetPosition();
		delta.y = 0.0f;
		const float radius = source->GetBoardAbilityRadius();
		if (radius > 0.0f && delta.LengthSquared() <= radius * radius)
		{
			return true;
		}
	}
	return false;
}

int EnemyBall::GetProtectionDamageReduction() const
{
	if (!IsProtectedByResonator()) return 0;
	int reduction = 0;
	for (const EnemyBall* source :
		GameRuntime::World().GetComponents<EnemyBall>())
	{
		if (source == nullptr || source == this || source->IsDefeated() ||
			source->IsPocketed() ||
			source->GetBoardAbilityType() !=
				EnemyBoardAbilityType::ProtectionResonator)
		{
			continue;
		}
		Vector3 delta = source->GetPosition() - GetPosition();
		delta.y = 0.0f;
		const float radius = source->GetBoardAbilityRadius();
		if (radius > 0.0f && delta.LengthSquared() <= radius * radius)
		{
			reduction = (std::max)(
				reduction,
				source->m_EnemyData.boardAbility.protectionDamageReduction);
		}
	}
	return reduction;
}

void EnemyBall::RefreshBoardAbilityState(bool notifyChange)
{
	const bool protectedNow = IsProtectedByResonator();
	const bool protectionInitialized = m_ProtectionState >= 0;
	const bool protectedBefore = m_ProtectionState > 0;
	m_ProtectionState = protectedNow ? 1 : 0;
	if (notifyChange && protectionInitialized &&
		protectedNow != protectedBefore)
	{
		GameBattleEvents::EnemyAbilityStateChanged({
			GetPosition(),
			reinterpret_cast<std::uintptr_t>(this),
			m_EnemyData.id,
			"protection_resonance",
			protectedNow ? "protection_applied" : "protection_removed",
			protectedBefore ? 1 : 0,
			protectedNow ? 1 : 0,
			GetAttack(),
			GetAttack(),
			0,
			0.0f,
		});
	}
	if (m_EnemyData.boardAbility.type !=
		EnemyBoardAbilityType::SwarmAmplifier || IsDefeated() || IsPocketed())
	{
		return;
	}
	const int previousStage = m_SwarmStage;
	const int previousAttack = GetAttack();
	int nearby = 0;
	const float radius = m_EnemyData.boardAbility.radius;
	for (const EnemyBall* ally :
		GameRuntime::World().GetComponents<EnemyBall>())
	{
		if (ally == nullptr || ally == this || ally->IsDefeated() ||
			ally->IsPocketed())
		{
			continue;
		}
		Vector3 delta = ally->GetPosition() - GetPosition();
		delta.y = 0.0f;
		if (radius > 0.0f && delta.LengthSquared() <= radius * radius)
		{
			++nearby;
		}
	}
	const bool initialized = m_NearbyLivingAllies >= 0;
	m_NearbyLivingAllies = nearby;
	m_SwarmStage = EnemyBoardAbilityRules::SwarmStage(nearby);
	if (notifyChange)
	{
		BalanceLogger::GetInstance().RecordEvent(
			"enemy_swarm_sample",
			{
				{ "enemy_id", m_EnemyData.id },
				{ "nearby_living_allies", nearby },
				{ "stage", m_SwarmStage },
				{ "attack", GetAttack() },
			});
	}
	if (notifyChange && initialized && m_SwarmStage != previousStage)
	{
		PublishBoardAbilityChange(
			"nearby_allies_changed",
			previousStage,
			previousAttack);
	}
}

void EnemyBall::RegisterWallImpact(float collisionSpeed)
{
	if (m_EnemyData.boardAbility.type !=
			EnemyBoardAbilityType::ImpactBrittle ||
		IsDefeated() || IsPocketed() ||
		!EnemyBoardAbilityRules::QualifiesForBrittle(
			collisionSpeed,
			m_EnemyData.boardAbility.brittleCollisionSpeed))
	{
		return;
	}
	const int previousStage = IsBrittle() ? 1 : 0;
	const int previousAttack = GetAttack();
	// Repeated qualifying impacts refresh the same one-action state. They do
	// not stack additional weak attacks.
	m_BrittleActionsRemaining =
		m_EnemyData.boardAbility.brittleActionCount;
	if (previousStage == 0)
	{
		PublishBoardAbilityChange(
			"qualifying_wall_impact",
			previousStage,
			previousAttack,
			collisionSpeed);
	}
}

void EnemyBall::ConsumeBoardAbilityAfterAttack()
{
	if (!CanParticipateInCombat() || !IsBrittle()) return;
	const int previousAttack = GetAttack();
	const int previousStage = 1;
	--m_BrittleActionsRemaining;
	if (!IsBrittle())
	{
		PublishBoardAbilityChange(
			"brittle_action_consumed",
			previousStage,
			previousAttack);
	}
}

void EnemyBall::PublishBoardAbilityChange(
	const char* reason,
	int previousStage,
	int previousAttack,
	float collisionSpeed) const
{
	const int currentStage =
		m_EnemyData.boardAbility.type ==
			EnemyBoardAbilityType::SwarmAmplifier
		? m_SwarmStage
		: (IsBrittle() ? 1 : 0);
	GameBattleEvents::EnemyAbilityStateChanged({
		GetPosition(),
		reinterpret_cast<std::uintptr_t>(this),
		m_EnemyData.id,
		ToString(m_EnemyData.boardAbility.type),
		reason != nullptr ? reason : "unknown",
		previousStage,
		currentStage,
		previousAttack,
		GetAttack(),
		GetNearbyLivingAllyCount(),
		collisionSpeed,
	});
}

void EnemyBall::SetPlannedAction(
    EnemyActionType action,
    int budgetCost,
    float chargeDamageMultiplier)
{
    if (!CanParticipateInCombat())
    {
        m_PlannedAction = EnemyActionType::Wait;
        m_PlannedActionCost = 0;
        return;
    }
    m_PlannedAction = action;
    m_PlannedActionCost = (std::max)(0, budgetCost);
    m_PlannedChargeDamageMultiplier =
        (std::max)(0.0f, chargeDamageMultiplier);
}

void EnemyBall::BeginCharge()
{
    if (m_PlannedAction != EnemyActionType::ChargeStart ||
        !CanUseCharge() || IsDefeated() || IsPocketed())
    {
        return;
    }
    m_ChargeState = EnemyChargeState::Charging;
}

void EnemyBall::CompleteChargeAttack()
{
    if (m_ChargeState == EnemyChargeState::Charging)
    {
        m_ChargeState = EnemyChargeState::Normal;
    }
    ResetChargeShotTracking();
}

void EnemyBall::BeginChargeShotTracking(bool heavyShot)
{
    ResetChargeShotTracking();
    m_PendingChargeInterruption.reset();
    if (m_ChargeState != EnemyChargeState::Charging ||
        m_PlannedAction != EnemyActionType::ChargeAttack ||
        IsDefeated() || IsPocketed())
    {
        return;
    }
    m_ChargeShotActive = true;
    m_ChargeShotHeavy = heavyShot;
    m_ChargeShotStart = GetPosition();
}

std::optional<ChargeInterruptionResult>
EnemyBall::EndChargeShotTracking(
    float heavyInterruptDiameterMultiplier,
    float pierceInterruptMaxHpRatio)
{
    if (!CanParticipateInCombat())
    {
        ResetChargeShotTracking();
        m_PendingChargeInterruption.reset();
        return std::nullopt;
    }
    if (!m_PendingChargeInterruption.has_value() &&
        m_ChargeState == EnemyChargeState::Charging &&
        !IsPocketed())
    {
        const ChargeInterruptionResult result = EnemyChargeRules::EvaluateShot(
            m_ChargeShotActive,
            m_ChargeShotHeavy,
            m_ChargeShotStart,
            GetPosition(),
            GetRadius(),
            GetMaxHP(),
            heavyInterruptDiameterMultiplier,
            pierceInterruptMaxHpRatio,
            m_ChargeShotPierceDamage);
        if (result.WasInterrupted())
        {
            m_PendingChargeInterruption = result;
            m_ChargeState = EnemyChargeState::Normal;
            m_PlannedAction = EnemyActionType::Wait;
            m_PlannedActionCost = 0;
        }
    }
    ResetChargeShotTracking();

    std::optional<ChargeInterruptionResult> result =
        m_PendingChargeInterruption;
    m_PendingChargeInterruption.reset();
    return result;
}

void EnemyBall::CancelCharge(ChargeInterruptReason reason)
{
    const bool wasCharging =
        m_ChargeState == EnemyChargeState::Charging;
    const bool hadChargePlan =
        m_PlannedAction == EnemyActionType::ChargeStart ||
        m_PlannedAction == EnemyActionType::ChargeAttack;
    if (!wasCharging && !hadChargePlan)
    {
        return;
    }

    if (wasCharging)
    {
        ChargeInterruptionResult result;
        result.reason = reason;
        result.pierceDamage = m_ChargeShotPierceDamage;
        m_PendingChargeInterruption = result;
    }
    m_ChargeState = EnemyChargeState::Normal;
    m_PlannedAction = EnemyActionType::Wait;
    m_PlannedActionCost = 0;
    ResetChargeShotTracking();
}

void EnemyBall::ResetChargeShotTracking()
{
    m_ChargeShotActive = false;
    m_ChargeShotHeavy = false;
    m_ChargeShotStart = Vector3::Zero;
    m_ChargeShotPierceDamage = 0;
}

int EnemyBall::AdjustCollisionDamage(int damage, const Vector3& sourcePosition) const
{
    if (!CanParticipateInCombat()) return 0;
    damage = BallPhysicsRules::DirectionalDamage(
        damage,
        GetPosition(),
        sourcePosition,
        m_EnemyData.frontalDamageMultiplier);
    return EnemyGimmickRules::ScaleCollisionDamage(
        damage,
        GetCollisionStageDamageMultiplier());
}

float EnemyBall::GetCollisionStageDamageMultiplier() const
{
    return EnemyGimmickRules::CollisionDamageMultiplier(
        m_EnemyData.collisionDamageMultipliers,
        m_CollisionStage);
}

void EnemyBall::RegisterCollisionForStage(std::uintptr_t otherBallId)
{
    if (!HasCollisionStages() || IsDefeated() || otherBallId == 0 ||
        m_CollisionGraceByBall.contains(otherBallId))
    {
        return;
    }

    ++m_CollisionStage;
    if (m_EnemyData.collisionCountGraceTicks > 0)
    {
        m_CollisionGraceByBall[otherBallId] =
            m_EnemyData.collisionCountGraceTicks;
    }
    UpdateCollisionStageTint();
}

void EnemyBall::UpdateCollisionStageTint()
{
	ApplyPresentationTint();
}

void EnemyBall::SetPresentationHitFlash(float intensity)
{
	m_PresentationHitFlash = std::clamp(intensity, 0.0f, 1.0f);
	ApplyPresentationTint();
}

void EnemyBall::ApplyPresentationTint()
{
	if (m_RenderComponent == nullptr) return;
    const float brightness = EnemyGimmickRules::CollisionStageBrightness(
        m_EnemyData.collisionDamageMultipliers,
        m_CollisionStage);
	const Color stageTint(
        m_BaseTint.x * brightness,
        m_BaseTint.y * brightness,
        m_BaseTint.z * brightness,
		m_BaseTint.w);
	const float warm = std::clamp(
		(m_PresentationHitFlash - 0.70f) / 0.30f,
		0.0f,
		1.0f);
	const Color flashTint(
		1.0f,
		1.0f - 0.06f * warm,
		1.0f - 0.30f * warm,
		stageTint.w);
	const float blend = m_PresentationHitFlash;
	m_RenderComponent->SetTint(Color(
		stageTint.x + (flashTint.x - stageTint.x) * blend,
		stageTint.y + (flashTint.y - stageTint.y) * blend,
		stageTint.z + (flashTint.z - stageTint.z) * blend,
		stageTint.w));
}

bool EnemyBall::HasLiveNuisanceBall() const
{
    return m_NuisanceBallObject != nullptr &&
        GameRuntime::World().Contains(m_NuisanceBallObject);
}

void EnemyBall::DestroyNuisanceBall()
{
    const bool destroyed = HasLiveNuisanceBall();
    if (destroyed)
    {
        m_NuisanceBallObject->Destroy();
    }
    m_NuisanceBallObject = nullptr;
    if (destroyed)
    {
        for (PlayerBall* player :
            GameRuntime::World().GetComponents<PlayerBall>())
            if (player != nullptr) player->RefreshNuisanceDebuffs();
    }
}

void EnemyBall::SpawnNuisanceBall()
{
    if (!m_EnemyData.nuisanceBall.enabled || HasLiveNuisanceBall() ||
        IsDefeated() || IsPocketed())
    {
        return;
    }
    NuisanceBall* nuisance = BallFactory::CreateNuisanceBall(
        m_EnemyData.nuisanceBall,
        m_NuisanceSpawnPosition);
    m_NuisanceBallObject = nuisance != nullptr
        ? nuisance->GetGameObject()
        : nullptr;
    for (PlayerBall* player :
        GameRuntime::World().GetComponents<PlayerBall>())
        if (player != nullptr) player->RefreshNuisanceDebuffs();
    m_NuisanceTurnsUntilSpawn = 0;
    m_NuisanceWarningVisible = false;
}

void EnemyBall::AdvanceTurnGimmicks()
{
    if (!CanParticipateInCombat()) return;
    if (m_SkipNextGimmickAdvance)
    {
        m_SkipNextGimmickAdvance = false;
        return;
    }
    if (m_StunTurnsRemaining > 0)
    {
        --m_StunTurnsRemaining;
        return;
    }
    if (!m_EnemyData.nuisanceBall.enabled || IsDefeated() || IsPocketed()) return;

    if (HasLiveNuisanceBall())
    {
        m_NuisanceWarningVisible = false;
        return;
    }
    m_NuisanceBallObject = nullptr;
    if (m_NuisanceTurnsUntilSpawn <= 0)
    {
        // The previous ball disappeared during the completed shot. Start the
        // full cooldown now; do not consume one turn on the same boundary.
        m_NuisanceTurnsUntilSpawn =
            (std::max)(1, m_EnemyData.nuisanceBall.respawnDelayTurns);
    }
    else
    {
        --m_NuisanceTurnsUntilSpawn;
    }

    if (m_NuisanceTurnsUntilSpawn == 0) SpawnNuisanceBall();
    else m_NuisanceWarningVisible = m_NuisanceTurnsUntilSpawn == 1;
}

void EnemyBall::DrawNuisanceWarning()
{
    if (!m_NuisanceWarningVisible || IsDefeated() || IsPocketed() ||
        m_RenderComponent == nullptr || m_Ball == nullptr)
    {
        return;
    }

    Vector3 delta = m_NuisanceSpawnPosition - GetPosition();
    delta.y = 0.0f;
    const float distance = delta.Length();
    if (distance > 0.001f)
    {
        delta /= distance;
        m_RenderComponent->SetTint(Color(1.0f, 0.8f, 0.15f, 0.55f));
        constexpr float spacing = 3.0f;
        for (float along = spacing; along < distance; along += spacing)
        {
            const Vector3 position = GetPosition() + delta * along + Vector3(0.0f, 0.15f, 0.0f);
            m_RenderComponent->DrawMesh(
                Matrix::CreateScale(0.28f) * Matrix::CreateTranslation(position));
        }
        const float ghostScale = (std::max)(0.6f, m_EnemyData.nuisanceBall.radius * 0.75f);
        m_RenderComponent->DrawMesh(
            Matrix::CreateScale(ghostScale) *
            Matrix::CreateTranslation(m_NuisanceSpawnPosition + Vector3(0.0f, 0.15f, 0.0f)));
        UpdateCollisionStageTint();
    }
}

int EnemyBall::ApplyPocketDamage()
{
    if (IsArmorBoss() || IsDefeated() || m_EnemyData.pocketDamageRatio <= 0.0f) return 0;
    const int hpBefore = GetHP();
    const int damage = BallMechanics::PocketDamage(GetMaxHP(), m_EnemyData.pocketDamageRatio);
    SetHP((std::max)(0, hpBefore - damage)); // 割合ダメージは防御・方向軽減を無視する。
    if (GetHP() == 0)
    {
        Defeat();
    }
    const int applied = hpBefore - GetHP();
    BalanceLogger::GetInstance().RecordEnemyDamage(GetEnemyId(), applied);
    return applied;
}

void EnemyBall::DrawImGui(const std::string& label)
{
    m_Ball->DrawImGui(label); // 共通UIを呼ぶ
}

void EnemyBall::HitBreakBall(int ballId)
{
    if (!IsArmorBoss() || IsDefeated()) return;
    const int hpBefore = GetHP(), armorBefore = m_BossState.armor;
    const bool started = m_BossState.HitBreakBall();
    const Vector3 velocity = GetVelocity();
    SetHP((std::max)(0, hpBefore - BossCombatRules::BreakBallDamage));
    if (GetHP() == 0)
    {
        Defeat();
        m_Ball->GetMutableVelocity() = velocity;
    }
    const int damage = hpBefore - GetHP();
    auto& logger = BalanceLogger::GetInstance();
    logger.RecordEnemyDamage(GetEnemyId(), damage);
    logger.RecordEvent("boss_break_ball_hit", {{"ball_id", ballId}, {"boss_id", GetEnemyId()},
        {"armor_before", armorBefore}, {"armor_after", m_BossState.armor},
        {"boss_damage", damage}, {"hp_before", hpBefore}, {"hp_after", GetHP()}});
    if (started) logger.RecordEvent("boss_break_started", {{"boss_id", GetEnemyId()},
        {"break_shots_remaining", m_BossState.shotsRemaining}, {"trigger_shot_excluded", true}});
    GameBattleEvents::CombatFeedback(
		GetPosition(), damage, IsDefeated(), false, this);
}

void EnemyBall::EndBossShot()
{
    if (IsArmorBoss() && !IsDefeated() && m_BossState.EndShot())
        BalanceLogger::GetInstance().RecordEvent("boss_break_ended",
            {{"boss_id", GetEnemyId()}, {"armor", m_BossState.armor}});
}
