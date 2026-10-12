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
		std::uintptr_t pinballTargetId = 0;
		int pinballCollisionCount = 0;
		int pinballCollisionsPerStage = 0;
		float pinballSpeedRetention = 0.9f;
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
		state.pinballTargetId = 0;
		state.pinballCollisionCount = 0;
		state.pinballCollisionsPerStage = 0;
		state.pinballSpeedRetention = 0.9f;
	}

	inline void EndPlayerShot(State& state)
	{
		state.activeSetupBonus = 0;
		state.setupTriggeredThisShot = false;
		state.empoweredHitsRemaining = 0;
		state.empoweredEnemyIds.clear();
		state.pinballTargetId = 0;
		state.pinballCollisionCount = 0;
		state.pinballCollisionsPerStage = 0;
		state.pinballSpeedRetention = 0.9f;
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

	inline bool ActivatePinball(
		State& state,
		std::uintptr_t targetId,
		int stackCost,
		int collisionsPerStage,
		float speedRetention)
	{
		if (targetId == 0 || state.pinballTargetId != 0 ||
			stackCost <= 0 || state.stackCount < stackCost)
		{
			return false;
		}
		state.stackCount -= stackCost;
		state.pinballTargetId = targetId;
		state.pinballCollisionCount = 0;
		state.pinballCollisionsPerStage = (std::max)(1, collisionsPerStage);
		state.pinballSpeedRetention = std::clamp(speedRetention, 0.0f, 1.0f);
		return true;
	}

	inline bool IsPinballTarget(const State& state, std::uintptr_t targetId)
	{
		return targetId != 0 && state.pinballTargetId == targetId;
	}

	inline int RegisterPinballCollision(State& state, std::uintptr_t targetId)
	{
		if (!IsPinballTarget(state, targetId)) return 0;
		++state.pinballCollisionCount;
		return state.pinballCollisionCount /
			(std::max)(1, state.pinballCollisionsPerStage);
	}

	inline void PreservePinballSpeed(
		const State& state,
		std::uintptr_t targetId,
		const Vector3& velocityBefore,
		Vector3& velocityAfter)
	{
		if (!IsPinballTarget(state, targetId)) return;
		const float beforeSpeed = velocityBefore.Length();
		const float afterSpeed = velocityAfter.Length();
		if (beforeSpeed <= 0.000001f || afterSpeed <= 0.000001f) return;
		const float correctedSpeed = std::clamp(
			afterSpeed,
			beforeSpeed * state.pinballSpeedRetention,
			beforeSpeed);
		velocityAfter *= correctedSpeed / afterSpeed;
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
