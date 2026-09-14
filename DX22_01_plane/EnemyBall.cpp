#include "EnemyBall.h"
#include "BallPhysicsRules.h"
#include "BallMechanics.h"
#include "EnemyGimmickRules.h"

#include "Camera.h"
#include "BallRenderComponent.h"
#include "BalanceLogger.h"
#include "Game.h"
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
    Draw(Game::GetCamera());
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
    m_BossState = {};
    m_IsPocketed = false;
    m_PocketEntryPosition = Vector3::Zero;
    m_CollisionStage = 0;
    m_CollisionGraceByBall.clear();
    m_NuisanceBallObject = nullptr;
    m_StunTurnsRemaining = 0;
    m_SkipNextGimmickAdvance = false;
    m_EnemyData = data;                        // 読み込んだ敵データを保持

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

    std::vector<Ground*> grounds = Game::GetInstance()->GetComponents<Ground>();
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

    m_Ball->ResetDefeated();                            // 撃破状態を解除

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
    if (cam == nullptr || m_Ball == nullptr || m_IsPocketed)
    {
        return;
    }

    cam->SetCamera();

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

    m_Ball->Defeat();
    m_NuisanceWarningVisible = false;
    DestroyNuisanceBall();
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
    if (m_EnemyData.id != data.id)
    {
        return;
    }

    // 敵データを更新
    m_EnemyData.status = data.status;
    m_EnemyData.maxHp = data.maxHp;
    m_EnemyData.frontalDamageMultiplier = data.frontalDamageMultiplier;
    m_EnemyData.pocketDamageRatio = data.pocketDamageRatio;
    m_EnemyData.collisionDamageMultipliers = data.collisionDamageMultipliers;
    m_EnemyData.collisionCountGraceTicks = data.collisionCountGraceTicks;
    m_EnemyData.nuisanceBall = data.nuisanceBall;
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
    if (m_Ball->IsDefeated())
    {
        // 撃破済みの場合はHP割合を計算せず、ステータス値だけ更新する
        m_Ball->SetMaxHP(m_EnemyData.maxHp);
        ApplyStatusValuesOnly(status);
        return;
    }

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
    // HPや撃破状態を初期化せず、ステータス構造体だけを差し替える
    m_Ball->ApplyStatusValuesOnly(status);
}

void EnemyBall::OnPocketHit()
{
    if (m_Ball == nullptr || m_IsPocketed)
    {
        return;
    }
    if (!IsArmorBoss()) Game::GetInstance()->HandleEnemyPocket(this);
}

void EnemyBall::EnterPocketQueue(const Vector3& pocketEntryPosition)
{
    if (m_Ball == nullptr || IsDefeated() || m_IsPocketed)
    {
        return;
    }
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

void EnemyBall::TakeDamage(int damage)
{
	if (m_Ball == nullptr)
	{
		return;
	}
	const int hpBefore = m_Ball->GetHP();
	const bool wasDefeated = m_Ball->IsDefeated();
	const Vector3 collisionVelocity =
		m_Ball->GetMutableVelocity();
	const Vector3 collisionAcceleration =
		m_Ball->GetMutableAcceleration();
    if (IsArmorBoss()) damage = BossCombatRules::DirectDamage(damage, GetDefense(), m_BossState) + GetDefense();
	m_Ball->TakeDamage(damage);

	// 通常ダメージで倒れた敵は、全ボールが止まるまで表示と物理判定を残す。
	// ポケットによる除外は別処理とし、即座に反映する。
	if (!wasDefeated && m_Ball->IsDefeated())
	{
		m_Ball->GetMutableVelocity() = collisionVelocity;
		m_Ball->GetMutableAcceleration() = collisionAcceleration;
		m_NuisanceWarningVisible = false;
		DestroyNuisanceBall();
		Game::GetInstance()->NotifyEnemyDefeated(m_EnemyData.id);
	}

	BalanceLogger::GetInstance().RecordEnemyDamage(
		m_EnemyData.id,
		(std::max)(0, hpBefore - m_Ball->GetHP()));
}

int EnemyBall::AdjustCollisionDamage(int damage, const Vector3& sourcePosition) const
{
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
    if (m_RenderComponent == nullptr) return;
    const float brightness = EnemyGimmickRules::CollisionStageBrightness(
        m_EnemyData.collisionDamageMultipliers,
        m_CollisionStage);
    m_RenderComponent->SetTint(Color(
        m_BaseTint.x * brightness,
        m_BaseTint.y * brightness,
        m_BaseTint.z * brightness,
        m_BaseTint.w));
}

bool EnemyBall::HasLiveNuisanceBall() const
{
    return m_NuisanceBallObject != nullptr &&
        Game::GetInstance()->ContainsGameObject(m_NuisanceBallObject);
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
        for (PlayerBall* player : Game::GetInstance()->GetComponents<PlayerBall>())
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
        *Game::GetInstance(),
        m_EnemyData.nuisanceBall,
        m_NuisanceSpawnPosition);
    m_NuisanceBallObject = nuisance != nullptr
        ? nuisance->GetGameObject()
        : nullptr;
    for (PlayerBall* player : Game::GetInstance()->GetComponents<PlayerBall>())
        if (player != nullptr) player->RefreshNuisanceDebuffs();
    m_NuisanceTurnsUntilSpawn = 0;
    m_NuisanceWarningVisible = false;
}

void EnemyBall::AdvanceTurnGimmicks()
{
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
        Game::GetInstance()->NotifyEnemyDefeated(GetEnemyId());
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
        Game::GetInstance()->NotifyEnemyDefeated(GetEnemyId());
    }
    const int damage = hpBefore - GetHP();
    auto& logger = BalanceLogger::GetInstance();
    logger.RecordEnemyDamage(GetEnemyId(), damage);
    logger.RecordEvent("boss_break_ball_hit", {{"ball_id", ballId}, {"boss_id", GetEnemyId()},
        {"armor_before", armorBefore}, {"armor_after", m_BossState.armor},
        {"boss_damage", damage}, {"hp_before", hpBefore}, {"hp_after", GetHP()}});
    if (started) logger.RecordEvent("boss_break_started", {{"boss_id", GetEnemyId()},
        {"break_shots_remaining", m_BossState.shotsRemaining}, {"trigger_shot_excluded", true}});
    Game::GetInstance()->NotifyCombatFeedback(GetPosition(), damage, IsDefeated(), false);
}

void EnemyBall::EndBossShot()
{
    if (IsArmorBoss() && !IsDefeated() && m_BossState.EndShot())
        BalanceLogger::GetInstance().RecordEvent("boss_break_ended",
            {{"boss_id", GetEnemyId()}, {"armor", m_BossState.armor}});
}
