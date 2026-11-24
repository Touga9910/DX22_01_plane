#pragma once

#include <algorithm>
#include <array>
#include <string_view>

enum class EnemyBoardAbilityType
{
	None,
	SwarmAmplifier,
	ImpactBrittle,
	ProtectionResonator,
};

/// <summary>
/// 敵の盤面能力を、設定やログで使う識別文字列に変換する。
/// </summary>
inline const char* ToString(EnemyBoardAbilityType ability)
{
	switch (ability)
	{
	case EnemyBoardAbilityType::SwarmAmplifier: return "swarm_amplifier";
	case EnemyBoardAbilityType::ImpactBrittle: return "impact_brittle";
	case EnemyBoardAbilityType::ProtectionResonator: return "protection_resonator";
	default: return "none";
	}
}

/// <summary>
/// 設定上の識別文字列から敵の盤面能力を求め、未知の値は能力なしとして扱う。
/// </summary>
inline EnemyBoardAbilityType EnemyBoardAbilityTypeFromString(
	std::string_view value)
{
	if (value == "swarm_amplifier")
		return EnemyBoardAbilityType::SwarmAmplifier;
	if (value == "impact_brittle")
		return EnemyBoardAbilityType::ImpactBrittle;
	if (value == "protection_resonator")
		return EnemyBoardAbilityType::ProtectionResonator;
	return EnemyBoardAbilityType::None;
}

namespace EnemyBoardAbilityRules
{
	/// <summary>
	/// 周囲の生存味方数に応じて、群体強化の段階を返す。
	/// </summary>
	inline int SwarmStage(int nearbyLivingAllies)
	{
		if (nearbyLivingAllies >= 4) return 2;
		if (nearbyLivingAllies >= 2) return 1;
		return 0;
	}

	/// <summary>
	/// 群体強化の段階に対応する攻撃力を、設定された上限の範囲で返す。
	/// </summary>
	inline int SwarmAttack(
		int nearbyLivingAllies,
		const std::array<int, 3>& attackByStage,
		int maximumAttack)
	{
		const int stage = SwarmStage((std::max)(0, nearbyLivingAllies));
		return std::clamp(
			attackByStage[static_cast<std::size_t>(stage)],
			0,
			(std::max)(0, maximumAttack));
	}

	/// <summary>
	/// 壁との衝突速度が脆化の発動しきい値に達しているか判定する。
	/// </summary>
	inline bool QualifiesForBrittle(float collisionSpeed, float threshold)
	{
		return collisionSpeed >= (std::max)(0.0f, threshold);
	}

	/// <summary>
	/// 防御計算後のダメージから防護共鳴の軽減量を引き、0以上にする。
	/// </summary>
	inline int ApplyProtectionReduction(
		int damageAfterDefense,
		int reduction)
	{
		return (std::max)(
			0,
			damageAfterDefense - (std::max)(0, reduction));
	}
}
