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

	/// <summary>
	/// 次のショット用の反射ボーナスを引き継ぎ、ショット単位の回数や効果を初期化する。
	/// </summary>
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

	/// <summary>
	/// ショット終了時に、使い残した現在のボーナスと強化命中・ピンボールの状態を解除する。
	/// </summary>
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

	/// <summary>
	/// 有効な反射を記録し、条件に応じてスタックを消費・生成して反射速度と強化命中回数を更新する。
	/// </summary>
	inline ReflectionResult ApplyValidReflection(
		State& state,
		const ReflectionConfig& config,
		Vector3& reflectedVelocity)
	{
		ReflectionResult result;
		++state.validReflectionsThisShot;
		if (config.consumesStacks && state.stackCount > 0 &&
			state.consumedThisShot < (std::max)(0, config.consumptionLimit))
		{
			--state.stackCount;
			++state.consumedThisShot;
			++state.empoweredHitsRemaining;
			reflectedVelocity *= (std::max)(1.0f, config.reflectedSpeedMultiplier);
			result.consumed = 1;
		}
		if (config.generatesStacks &&
			state.generatedThisShot < (std::max)(0, config.generationLimit))
		{
			result.generated = (std::max)(0, config.stacksPerReflection);
			state.stackCount += result.generated;
			++state.generatedThisShot;
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

	/// <summary>
	/// 同じ敵への重複適用を避け、使用可能なら強化命中の残り回数を1減らす。
	/// </summary>
	inline bool ConsumeEmpoweredHit(State& state, std::uintptr_t enemyId)
	{
		if (enemyId == 0 || state.empoweredHitsRemaining <= 0 ||
			state.empoweredEnemyIds.contains(enemyId)) return false;
		state.empoweredEnemyIds.insert(enemyId);
		--state.empoweredHitsRemaining;
		return true;
	}

	/// <summary>
	/// 次のショットで有効な反射が起きたときに加算するスタック数を予約する。
	/// </summary>
	inline void ArmSetupForNextShot(State& state, int amount)
	{
		state.pendingSetupBonus = (std::max)(0, amount);
	}

	/// <summary>
	/// スタック消費と対象の条件を確認し、ピンボールの対象と衝突条件を登録する。
	/// </summary>
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

	/// <summary>
	/// 指定された識別子が、現在のピンボール効果の対象か判定する。
	/// </summary>
	inline bool IsPinballTarget(const State& state, std::uintptr_t targetId)
	{
		return targetId != 0 && state.pinballTargetId == targetId;
	}

	/// <summary>
	/// ピンボール対象の衝突を加算し、所定の衝突数ごとに進む段階を返す。
	/// </summary>
	inline int RegisterPinballCollision(State& state, std::uintptr_t targetId)
	{
		if (!IsPinballTarget(state, targetId)) return 0;
		++state.pinballCollisionCount;
		return state.pinballCollisionCount /
			(std::max)(1, state.pinballCollisionsPerStage);
	}

	/// <summary>
	/// ピンボール対象の衝突後速度を、保持率に基づく下限から衝突前の速さまでの範囲に補正する。
	/// </summary>
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

	/// <summary>
	/// 反射チャージのスタックがあれば1、なければ0を返す。
	/// </summary>
	inline int ActiveCount(const State& state)
	{
		return state.stackCount > 0 ? 1 : 0;
	}

	/// <summary>
	/// 次のショット用の追加スタックが予約されていれば1、なければ0を返す。
	/// </summary>
	inline int PendingNextShotCount(const State& state)
	{
		return state.pendingSetupBonus > 0 ? 1 : 0;
	}

	/// <summary>
	/// 保持する反射チャージのスタック数を0以上で返す。
	/// </summary>
	inline int TotalStacks(const State& state)
	{
		return (std::max)(0, state.stackCount);
	}
}
