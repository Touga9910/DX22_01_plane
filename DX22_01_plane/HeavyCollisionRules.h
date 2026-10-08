#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

// 重量軸の永続スタック、ショット中の敵別移動距離、一時効果を管理する。
namespace HeavyCollisionRules
{
	static constexpr float DistancePerStack = 5.0f;

	struct State
	{
		int stackCount = 0;
		std::unordered_map<std::uintptr_t, float> distanceByEnemyThisShot;
		std::unordered_set<std::uintptr_t> microGeneratedEnemiesThisShot;
		float pendingEnemyCollisionMultiplier = 1.0f;
		float activeEnemyCollisionMultiplier = 1.0f;
	};

	inline void Reset(State& state)
	{
		state = {};
	}

	inline void BeginShot(State& state)
	{
		state.distanceByEnemyThisShot.clear();
		state.microGeneratedEnemiesThisShot.clear();
		state.activeEnemyCollisionMultiplier =
			(std::max)(1.0f, state.pendingEnemyCollisionMultiplier);
		state.pendingEnemyCollisionMultiplier = 1.0f;
	}

	inline void RecordEnemyMovement(
		State& state,
		std::uintptr_t enemyId,
		float distance,
		bool generatesDistanceStacks)
	{
		if (!generatesDistanceStacks || enemyId == 0 || distance <= 0.0f) return;
		state.distanceByEnemyThisShot[enemyId] += distance;
	}

	inline int EndShot(State& state)
	{
		int generated = 0;
		for (const auto& [enemyId, distance] : state.distanceByEnemyThisShot)
		{
			(void)enemyId;
			generated += (std::max)(0, static_cast<int>(
				std::floor(distance / DistancePerStack)));
		}
		state.stackCount += generated;
		state.distanceByEnemyThisShot.clear();
		state.microGeneratedEnemiesThisShot.clear();
		state.activeEnemyCollisionMultiplier = 1.0f;
		return generated;
	}

	inline bool GenerateOncePerEnemy(
		State& state,
		std::uintptr_t enemyId,
		int amount = 1)
	{
		if (enemyId == 0 || amount <= 0 ||
			!state.microGeneratedEnemiesThisShot.insert(enemyId).second)
		{
			return false;
		}
		state.stackCount += amount;
		return true;
	}

	inline int Consume(State& state, int amount)
	{
		const int requested = (std::max)(0, amount);
		const int consumed = (std::min)(state.stackCount, requested);
		state.stackCount -= consumed;
		return consumed;
	}

	inline void ArmNextEnemyCollisionMultiplier(State& state, float multiplier)
	{
		state.pendingEnemyCollisionMultiplier = (std::max)(1.0f, multiplier);
	}

	inline int ScaleEnemyCollisionDamage(const State& state, int damage)
	{
		return (std::max)(0, static_cast<int>(std::lround(
			static_cast<float>(damage) * state.activeEnemyCollisionMultiplier)));
	}

	struct FinisherResult
	{
		int referenced = 0;
		int consumed = 0;
		int bonusDamage = 0;
	};

	inline FinisherResult ConsumeForFinisher(
		State& state,
		float damagePerStack,
		int consumeAmount)
	{
		FinisherResult result;
		result.referenced = (std::max)(0, state.stackCount);
		result.consumed = Consume(state, consumeAmount);
		result.bonusDamage = (std::max)(0, static_cast<int>(
			damagePerStack * static_cast<float>(result.consumed) + 0.5f));
		return result;
	}
}
