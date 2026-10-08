#pragma once

#include "SimpleMath.h"

#include <algorithm>
#include <cstdint>
#include <unordered_set>

namespace CushionChargeRules
{
	using DirectX::SimpleMath::Vector3;

	struct State
	{
		int stackCount = 0;
		int pendingSetupBonus = 0;
		int activeSetupBonus = 0;
		bool setupTriggeredThisShot = false;
		int validReflectionsThisShot = 0;
		int generatedThisShot = 0;
		int consumedThisShot = 0;
		int empoweredHitsRemaining = 0;
		std::unordered_set<std::uintptr_t> empoweredEnemyIds;
	};

	struct ReflectionConfig
	{
		bool generatesStacks = false;
		int generationLimit = 0;
		int stacksPerReflection = 0;
		bool consumesStacks = false;
		int consumptionLimit = 0;
		float reflectedSpeedMultiplier = 1.0f;
	};

	struct ReflectionResult
	{
		int generated = 0;
		int consumed = 0;
		bool setupTriggered = false;
	};

	inline void BeginPlayerShot(State& state)
	{
		state.activeSetupBonus = state.pendingSetupBonus;
		state.pendingSetupBonus = 0;
		state.setupTriggeredThisShot = false;
		state.validReflectionsThisShot = 0;
		state.generatedThisShot = 0;
		state.consumedThisShot = 0;
		state.empoweredHitsRemaining = 0;
		state.empoweredEnemyIds.clear();
	}

	inline void EndPlayerShot(State& state)
	{
		state.activeSetupBonus = 0;
		state.setupTriggeredThisShot = false;
		state.empoweredHitsRemaining = 0;
		state.empoweredEnemyIds.clear();
	}

	inline ReflectionResult ApplyValidReflection(
		State& state,
		const ReflectionConfig& config,
		Vector3& reflectedVelocity)
	{
		ReflectionResult result;
		++state.validReflectionsThisShot;
		if (config.generatesStacks &&
			state.generatedThisShot < (std::max)(0, config.generationLimit))
		{
			result.generated = (std::max)(0, config.stacksPerReflection);
			state.stackCount += result.generated;
			++state.generatedThisShot;
		}
		if (config.consumesStacks && state.stackCount > 0 &&
			state.consumedThisShot < (std::max)(0, config.consumptionLimit))
		{
			--state.stackCount;
			++state.consumedThisShot;
			++state.empoweredHitsRemaining;
			reflectedVelocity *= (std::max)(1.0f, config.reflectedSpeedMultiplier);
			result.consumed = 1;
		}
		if (!state.setupTriggeredThisShot && state.activeSetupBonus > 0)
		{
			state.stackCount += state.activeSetupBonus;
			result.generated += state.activeSetupBonus;
			result.setupTriggered = true;
			state.setupTriggeredThisShot = true;
			state.activeSetupBonus = 0;
		}
		return result;
	}

	inline bool ConsumeEmpoweredHit(State& state, std::uintptr_t enemyId)
	{
		if (enemyId == 0 || state.empoweredHitsRemaining <= 0 ||
			state.empoweredEnemyIds.contains(enemyId)) return false;
		state.empoweredEnemyIds.insert(enemyId);
		--state.empoweredHitsRemaining;
		return true;
	}

	inline void ArmSetupForNextShot(State& state, int amount)
	{
		state.pendingSetupBonus = (std::max)(0, amount);
	}

	inline int ActiveCount(const State& state)
	{
		return state.stackCount > 0 ? 1 : 0;
	}

	inline int PendingNextShotCount(const State& state)
	{
		return state.pendingSetupBonus > 0 ? 1 : 0;
	}

	inline int TotalStacks(const State& state)
	{
		return (std::max)(0, state.stackCount);
	}
}
