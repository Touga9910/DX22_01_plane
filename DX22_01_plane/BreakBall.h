#pragma once

#include "Component.h"
#include "SimpleMath.h"

class BallComponent;
class EnemyBall;

// Neutral prop: never participates in enemy attacks, rewards, or victory counts.
class BreakBall final : public Component
{
public:
    explicit BreakBall(int index) : m_Index(index) {}
    void Awake() override;
    void FixedUpdate() override;
    void Draw() override;
    void OnDestroy() override;
    void HitBoss(EnemyBall& boss);
    void Deactivate(bool pocketed);
    void PlaceAt(const DirectX::SimpleMath::Vector3& position);
    bool Reposition();
    int GetIndex() const { return m_Index; }
    bool IsPocketed() const { return m_Pocketed; }
    bool IsUsed() const { return m_Used; }
    BallComponent* GetBall() const { return m_Ball; }
private:
    BallComponent* m_Ball = nullptr;
    int m_Index = 0;
    bool m_Pocketed = false, m_Used = false;
    bool m_HasPreferredPosition = false;
    DirectX::SimpleMath::Vector3 m_PreferredPosition = DirectX::SimpleMath::Vector3::Zero;
};
