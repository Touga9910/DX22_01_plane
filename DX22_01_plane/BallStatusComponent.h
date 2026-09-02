#pragma once

#include <algorithm>

#include "BallStatus.h"
#include "Component.h"

// ボールの戦闘パラメータと実行時HPを公開するデータコンポーネント。
// BallComponentが値を更新し、他のComponentはこの型を参照する。
class BallStatusComponent final : public Component
{
public:
    void SetStatus(const BallStatus& status)
    {
        ApplyStatusValuesOnly(status);
        m_CurrentHp = m_Status.maxHp;
        m_IsDefeated = false;
    }

    void ApplyStatusValuesOnly(const BallStatus& status)
    {
        m_Status = NormalizeBallStatus(status);
        m_CurrentHp = std::clamp(m_CurrentHp, 0, m_Status.maxHp);
    }

    void Synchronize(const BallStatus& status, int currentHp, bool defeated)
    {
        m_Status = NormalizeBallStatus(status);
        m_CurrentHp = std::clamp(currentHp, 0, m_Status.maxHp);
        m_IsDefeated = defeated;
    }

    const BallStatus& GetStatus() const { return m_Status; }
    int GetCurrentHp() const { return m_CurrentHp; }
    int GetMaxHp() const { return m_Status.maxHp; }
    int GetAttack() const { return m_Status.attack + m_AttackModifier; }
    int GetDefense() const { return m_Status.defense + m_DefenseModifier; }
    bool IsDefeated() const { return m_IsDefeated; }

    void SetCombatModifiers(int attackModifier, int defenseModifier)
    {
        m_AttackModifier = (std::max)(0, attackModifier);
        m_DefenseModifier = (std::max)(0, defenseModifier);
    }

    void SetCurrentHp(int hp)
    {
        m_CurrentHp = std::clamp(hp, 0, m_Status.maxHp);
    }

    void SetMaxHp(int maxHp)
    {
        m_Status.maxHp = (std::max)(1, maxHp);
        m_CurrentHp = (std::min)(m_CurrentHp, m_Status.maxHp);
    }

    void ResetDefeated() { m_IsDefeated = false; }
    void MarkDefeated()
    {
        m_IsDefeated = true;
        m_CurrentHp = 0;
    }

    int CalculateDamageTaken(int damage) const
    {
        return (std::max)(1, damage - GetDefense());
    }

    bool ApplyDamage(int damage)
    {
        if (m_IsDefeated) return false;
        const int finalDamage = CalculateDamageTaken(damage);
        m_CurrentHp -= finalDamage;
        if (m_CurrentHp <= 0)
        {
            m_CurrentHp = 0;
            return true;
        }
        return false;
    }

private:
    BallStatus m_Status{};
    int m_AttackModifier = 0;
    int m_DefenseModifier = 0;
    int m_CurrentHp = 0;
    bool m_IsDefeated = false;
};
