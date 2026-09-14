#pragma once

#include "Component.h"
#include "EnemyData.h"

class BallComponent;

// A neutral physical ball. Merely remaining on the table contributes its
// configured debuff aura; contacts never deal damage or increase enemy stages.
class NuisanceBall final : public Component
{
public:
    NuisanceBall(const NuisanceBallData& data,
        const DirectX::SimpleMath::Vector3& position)
        : m_Data(data), m_Position(position) {}

    void Awake() override;
    void FixedUpdate() override;
    void Draw() override;
    void OnDestroy() override;

    const StatusEffectCollection& GetDebuffs() const { return m_Data.debuffs; }
    BallComponent* GetBall() const { return m_Ball; }

private:
    void Pocket();

    NuisanceBallData m_Data;
    DirectX::SimpleMath::Vector3 m_Position;
    BallComponent* m_Ball = nullptr;
};
