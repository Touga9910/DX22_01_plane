#pragma once

#include <algorithm>

#include "BallStatus.h"
#include "Component.h"

// ボールの戦闘パラメータと実行時HPを公開するデータコンポーネント。
// BallComponentが値を更新し、他のComponentはこの型を参照する。
class BallStatusComponent final : public Component
{
public:
    /// <summary>
    /// ボールのステータス値を設定し、HPを最大まで回復して撃破状態を解除する。
    /// </summary>
    void SetStatus(const BallStatus& status)
    {
        ApplyStatusValuesOnly(status);
        m_CurrentHp = m_MaxHp;
        m_IsDefeated = false;
    }

    /// <summary>
    /// ステータス値を正規化して設定し、現在HPを有効範囲に収める。
    /// </summary>
    void ApplyStatusValuesOnly(const BallStatus& status)
    {
        m_Status = NormalizeBallStatus(status);
        m_CurrentHp = std::clamp(m_CurrentHp, 0, m_MaxHp);
    }

    /// <summary>
    /// 指定のステータス値、現在HP、撃破状態をまとめて反映する。
    /// </summary>
    void Synchronize(const BallStatus& status, int currentHp, bool defeated)
    {
        m_Status = NormalizeBallStatus(status);
        m_CurrentHp = std::clamp(currentHp, 0, m_MaxHp);
        m_IsDefeated = defeated;
    }

    /// <summary>
    /// 保存されているボールのステータスを読み取り専用の参照で返す。
    /// </summary>
    const BallStatus& GetStatus() const { return m_Status; }
    /// <summary>
    /// ボールの現在HPを返す。
    /// </summary>
    int GetCurrentHp() const { return m_CurrentHp; }
    /// <summary>
    /// ボールの最大HPを返す。
    /// </summary>
    int GetMaxHp() const { return m_MaxHp; }
    /// <summary>
    /// 基礎攻撃力に戦闘補正を加えた値を返す。
    /// </summary>
    int GetAttack() const { return m_Status.attack + m_AttackModifier; }
    /// <summary>
    /// 基礎防御力に戦闘補正を加えた値を返す。
    /// </summary>
    int GetDefense() const { return m_Status.defense + m_DefenseModifier; }
    /// <summary>
    /// ボールの撃破フラグを返す。
    /// </summary>
    bool IsDefeated() const { return m_IsDefeated; }

    /// <summary>
    /// 攻撃力と防御力の戦闘補正を非負の値で設定する。
    /// </summary>
    void SetCombatModifiers(int attackModifier, int defenseModifier)
    {
        m_AttackModifier = (std::max)(0, attackModifier);
        m_DefenseModifier = (std::max)(0, defenseModifier);
    }

    /// <summary>
    /// 現在HPをゼロから最大HPまでの範囲に収めて設定する。
    /// </summary>
    void SetCurrentHp(int hp)
    {
        m_CurrentHp = std::clamp(hp, 0, m_MaxHp);
    }

    /// <summary>
    /// 最大HPを最低1で設定し、現在HPが上限を超えた分を切り詰める。
    /// </summary>
    void SetMaxHp(int maxHp)
    {
        m_MaxHp = (std::max)(1, maxHp);
        m_CurrentHp = (std::min)(m_CurrentHp, m_MaxHp);
    }

    /// <summary>
    /// ボールの撃破フラグを解除する。
    /// </summary>
    void ResetDefeated() { m_IsDefeated = false; }
    /// <summary>
    /// ボールを撃破状態にして現在HPをゼロにする。
    /// </summary>
    void MarkDefeated()
    {
        m_IsDefeated = true;
        m_CurrentHp = 0;
    }

    /// <summary>
    /// ダメージから防御力を差し引き、最終被ダメージを最低1に揃える。
    /// </summary>
    int CalculateDamageTaken(int damage) const
    {
        return (std::max)(1, damage - GetDefense());
    }

    /// <summary>
    /// 未撃破のボールに防御計算を適用してHPを減らし、HPが尽きたかを返す。
    /// </summary>
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

	// Applies damage that has already passed defense and temporary-shield rules.
	/// <summary>
	/// 未撃破のボールに計算済みの正のダメージを適用し、HPが尽きたかを返す。
	/// </summary>
	bool ApplyFinalDamage(int damage)
	{
		if (m_IsDefeated || damage <= 0) return false;
		m_CurrentHp -= damage;
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
    int m_MaxHp = 1; // ラン情報または敵データから設定される実行時HP上限
    int m_CurrentHp = 0;
    bool m_IsDefeated = false;
};
