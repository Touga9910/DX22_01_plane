#pragma once

#include "Component.h"
#include "EnemyData.h"

class BallComponent;

// A neutral physical ball. Merely remaining on the table contributes its
// configured debuff aura; contacts never deal damage or increase enemy stages.
class NuisanceBall final : public Component
{
public:
    /// <summary>
    /// お邪魔ボールのデータと初期位置を保存する。
    /// </summary>
    NuisanceBall(const NuisanceBallData& data,
        const DirectX::SimpleMath::Vector3& position)
        : m_Data(data), m_Position(position) {}

    void Awake() override;
    void FixedUpdate() override;
    void Draw() override;
    void OnDestroy() override;

    /// <summary>
    /// お邪魔ボールが付与する弱体効果の一覧を返す。
    /// </summary>
    const StatusEffectCollection& GetDebuffs() const { return m_Data.debuffs; }
    /// <summary>
    /// お邪魔ボールの物理コンポーネントを返す。
    /// </summary>
    BallComponent* GetBall() const { return m_Ball; }

private:
    void Pocket();

    NuisanceBallData m_Data;
    DirectX::SimpleMath::Vector3 m_Position;
    BallComponent* m_Ball = nullptr;
};
