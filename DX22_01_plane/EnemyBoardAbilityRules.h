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
	inline int SwarmStage(int nearbyLivingAllies)
	{
		if (nearbyLivingAllies >= 4) return 2;
		if (nearbyLivingAllies >= 2) return 1;
		return 0;
	}

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

	inline bool QualifiesForBrittle(float collisionSpeed, float threshold)
	{
		return collisionSpeed >= (std::max)(0.0f, threshold);
	}

	inline int ApplyProtectionReduction(
		int damageAfterDefense,
		int reduction)
	{
		return (std::max)(
			0,
			damageAfterDefense - (std::max)(0, reduction));
	}
}
