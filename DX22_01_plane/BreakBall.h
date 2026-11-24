#pragma once

#include "Component.h"
#include "SimpleMath.h"

class BallComponent;
class EnemyBall;

// Neutral prop: never participates in enemy attacks, rewards, or victory counts.
class BreakBall final : public Component
{
public:
    /// <summary>
    /// 補助球を識別する番号を保持して構築する。
    /// </summary>
    explicit BreakBall(int index) : m_Index(index) {}
    void Awake() override;
    void FixedUpdate() override;
    void Draw() override;
    void OnDestroy() override;
    void HitBoss(EnemyBall& boss);
    void Deactivate(bool pocketed);
    void PlaceAt(const DirectX::SimpleMath::Vector3& position);
    bool Reposition();
    /// <summary>
    /// 補助球の識別番号を返す。
    /// </summary>
    int GetIndex() const { return m_Index; }
    /// <summary>
    /// 補助球がポケットに入った状態かを返す。
    /// </summary>
    bool IsPocketed() const { return m_Pocketed; }
    /// <summary>
    /// 補助球が装甲への攻撃に使用された状態かを返す。
    /// </summary>
    bool IsUsed() const { return m_Used; }
    /// <summary>
    /// 補助球を構成する球コンポーネントを返す。
    /// </summary>
    BallComponent* GetBall() const { return m_Ball; }
private:
    BallComponent* m_Ball = nullptr;
    int m_Index = 0;
    bool m_Pocketed = false, m_Used = false;
    bool m_HasPreferredPosition = false;
    DirectX::SimpleMath::Vector3 m_PreferredPosition = DirectX::SimpleMath::Vector3::Zero;
};
