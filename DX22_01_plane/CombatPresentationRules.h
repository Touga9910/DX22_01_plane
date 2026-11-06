#pragma once

#include <algorithm>
#include <cstdint>

// Pure presentation rules for damage reactions. These values never feed back
// into HP, collision, movement, or battle resolution.
namespace CombatPresentationRules
{
	struct HitFlash
	{
		float duration = 0.0f;
		float intensity = 0.0f;
	};

	inline HitFlash FlashForDamage(int appliedDamage)
	{
		if (appliedDamage <= 0) return {};
		if (appliedDamage == 1) return { 0.08f, 0.52f };
		if (appliedDamage <= 3) return { 0.12f, 0.72f };
		return { 0.16f, 0.92f };
	}

	inline int MultiDefeatTier(int uniqueDefeats)
	{
		if (uniqueDefeats >= 4) return 3;
		if (uniqueDefeats >= 3) return 2;
		if (uniqueDefeats >= 2) return 1;
		return 0;
	}

	inline constexpr float DamageMergeWindowSeconds = 0.16f;
	inline bool CanMergeDamage(
		std::uintptr_t existingTarget,
		std::uintptr_t incomingTarget,
		float ageSeconds,
		float separationSquared,
		bool defeated)
	{
		return !defeated && incomingTarget != 0 &&
			existingTarget == incomingTarget &&
			ageSeconds >= 0.0f &&
			ageSeconds <= DamageMergeWindowSeconds &&
			separationSquared <= 4.0f;
	}

	inline constexpr int MaximumHitReactions = 16;
	inline constexpr int MaximumTrackedDefeats = 32;
	inline constexpr int MaximumFloatingDamageNumbers = 8;
}
