#pragma once

#include "BallComponent.h"
#include "EnemyData.h"
#include "BossCombatRules.h"

#include <string>
#include <optional>

class PlayerBall;
class Camera;
class BallRenderComponent;

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
    void TakeDamage(int damage);
    bool IsArmorBoss() const { return m_EnemyData.id == "enemy_boss_core"; }
    const BossCombatRules::State& GetBossState() const { return m_BossState; }
    void SetDebugBossState(int armor, int breakShots)
    {
        if (!IsArmorBoss()) return;
        m_BossState = {};
        m_BossState.shotsRemaining = std::clamp(breakShots, 0, BossCombatRules::BreakShots);
        m_BossState.armor = m_BossState.IsBroken() ? 0 : std::clamp(armor, 1, BossCombatRules::MaxArmor);
    }
    void BeginBossShot() { if (IsArmorBoss()) m_BossState.BeginShot(); }
    void EndBossShot();
    void HitBreakBall(int ballId);
    int AdjustCollisionDamage(int damage, const DirectX::SimpleMath::Vector3& sourcePosition) const;
    int ApplyPocketDamage();
    float GetPocketDamageRatio() const { return m_EnemyData.pocketDamageRatio; }
    float GetFrontalDamageMultiplier() const { return m_EnemyData.frontalDamageMultiplier; }
    void OnPocketHit();
    void EnterPocketQueue();
    void ReturnFromPocket(
        const DirectX::SimpleMath::Vector3& position);

    void SetStatus(const BallStatus& status) { m_Ball->SetStatus(status); }
    const BallStatus& GetStatus() const { return m_Ball->GetStatus(); }
    void SetHP(int hp) { m_Ball->SetHP(hp); }
    int GetHP() const { return m_Ball->GetHP(); }
    int GetMaxHP() const { return m_Ball->GetMaxHP(); }
    int GetAttack() const { return m_Ball->GetAttack(); }
    int GetDefense() const { return m_Ball->GetDefense(); }
    bool IsDefeated() const { return m_Ball->IsDefeated(); }
    bool IsPocketed() const { return m_IsPocketed; }
    bool IsStopped() const { return m_Ball->IsStopped(); }
    float GetRadius() const { return m_Ball->GetRadius(); }
    DirectX::SimpleMath::Vector3 GetVelocity() const { return m_Ball->GetVelocity(); }
    DirectX::SimpleMath::Vector3 GetPosition() const { return m_Ball->GetPosition(); }
    BallComponent* GetBall() const { return m_Ball; }

    // -------------------------
    // 敵データ・ステータス
    // -------------------------
    const std::string& GetEnemyId() const { return m_EnemyData.id; }
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

private:
    BallComponent* m_Ball = nullptr;
    BallRenderComponent* m_RenderComponent = nullptr;
    std::optional<EnemyData> m_InitialData;
    int m_CurrentFrame = 0;                                                                    // 経過フレーム
    EnemyData m_EnemyData;                                                                     // 敵データ
    DirectX::SimpleMath::Vector3 m_InitPosition = DirectX::SimpleMath::Vector3(50.0f, 0.0f, 50.0f); // 敵の初期位置
    bool m_IsPocketed = false;
    BossCombatRules::State m_BossState;
};
